// Required to send https traffic to open
#define CPPHTTPLIB_OPENSSL_SUPPORT

#include "httplib/httplib.h"
#include "matrix/include/led-matrix.h"
#include "json/single_include/nlohmann/json.hpp"

#include <getopt.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <Magick++.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) { interrupt_received = true; }

template <size_t N = 104> class YuleGenerator {
public:
  static YuleGenerator create(std::shared_ptr<rgb_matrix::RGBMatrix> matrix,
                              const fs::path bootstrapImgs,
                              int targetWidth = 32, int targetHeight = 32,
                              const std::string &openaiApiKey = std::string{},
                              int imgsPerHour = 20) {
    Magick::InitializeMagick(NULL);

    // bootstrap imgs
    std::vector<rgb_matrix::FrameCanvas *> canvases{};
    for (const auto &img : images(bootstrapImgs, targetWidth, targetHeight)) {
      canvases.emplace_back(canvas(img, matrix->CreateFrameCanvas()));
    }
    return YuleGenerator{std::move(matrix), std::move(canvases), targetWidth,
                         targetHeight,      openaiApiKey,        imgsPerHour};
  }

  std::optional<rgb_matrix::FrameCanvas *> next() {
    // if a new genai image exists, render that
    if (!genImgs_.empty()) {
      std::scoped_lock l{genImgMutex_};
      if (!genImgs_.empty()) {
        const auto img = genImgs_.front();
        genImgs_.pop();
        imgs_.emplace_back(
            img); // make the gen image part of regular images vector
        return img;
      }
    }
    // else, render from bootstrapped or previous genai images
    if (!imgs_.empty()) {
      return imgs_.at(
          imgDist_(rng_, std::uniform_int_distribution<size_t>::param_type(
                             0, imgs_.size() - 1)));
    }
    return std::nullopt;
  }

  YuleGenerator(std::shared_ptr<rgb_matrix::RGBMatrix> matrix,
                std::vector<rgb_matrix::FrameCanvas *> &&canvases,
                int targetWidth, int targetHeight,
                const std::string &opeaiApiKey, int imgsPerHour)
      : matrix_{std::move(matrix)}, imgs_{std::move(canvases)},
        targetWidth_{targetWidth}, targetHeight_{targetHeight},
        rng_{static_cast<size_t>(
            std::chrono::system_clock::now().time_since_epoch().count())} {
    genThrd_ =
        std::thread(&YuleGenerator::genImgLoop, this, opeaiApiKey, imgsPerHour);
  }

  ~YuleGenerator() { genThrd_.join(); }

private:
  static std::vector<Magick::Image> images(fs::path p, int targetWidth,
                                           int targetHeight) {
    std::vector<Magick::Image> imgs{};
    for (const auto &file : fs::directory_iterator(p)) {
      if (!file.is_regular_file()) {
        std::cout << "Skipping '" << file.path()
                  << "' as it is not a regular file" << std::endl;
        continue;
      }
      auto img = image(file.path(), targetWidth, targetHeight);
      if (!img) {
        std::cout << "Skipping '" << file.path() << "' as it is empty"
                  << std::endl;
        continue;
      }
      imgs.emplace_back(std::move(*img));
    }
    return imgs;
  }

  static std::optional<Magick::Image> image(fs::path p, int targetWidth,
                                            int targetHeight) {
    std::vector<Magick::Image> imgs{};
    Magick::readImages(&imgs, p);
    if (imgs.empty()) {
      return std::nullopt;
    }
    scale_image(imgs.at(0), targetWidth, targetHeight);
    return std::move(imgs.at(0));
  }

  static Magick::Image &scale_image(Magick::Image &img, int width, int height) {
    int w = img.columns();
    int h = img.rows();
    float wf = width / static_cast<float>(w);
    float hf = width / static_cast<float>(h);
    float fraction = (wf > hf) ? wf : hf; // choose the largest factor
    img.scale(Magick::Geometry(static_cast<int>(fraction * w),
                               static_cast<int>(fraction * h)));
    return img;
  }

  static rgb_matrix::FrameCanvas *canvas(const Magick::Image &img,
                                         rgb_matrix::FrameCanvas *canvas) {
    canvas->Clear();
    for (size_t y = 0; y < img.rows(); ++y) {
      for (size_t x = 0; x < img.columns(); ++x) {
        const Magick::Color &c = img.pixelColor(x, y);
        if (c.alphaQuantum() < 255) {
          canvas->SetPixel(x, y, ScaleQuantumToChar(c.redQuantum()),
                           ScaleQuantumToChar(c.greenQuantum()),
                           ScaleQuantumToChar(c.blueQuantum()));
        }
      }
    }
    return canvas;
  }

  void genImgLoop(const std::string apiKey, int imgsPerHour) {
    if (apiKey.empty()) {
      std::cerr << "Not running genai loop as there is no api key" << std::endl;
      return;
    }

    fs::path outDir =
        fs::temp_directory_path() / ("gen-imgs-" + std::to_string(getpid()));
    std::error_code ec;
    if (!fs::create_directory(outDir, fs::temp_directory_path(), ec)) {
      if (ec) {
        std::cerr << "Cannot create '" << outDir << "' " << ec.message()
                  << std::endl;
        return;
      } else {
        std::cerr << "Directory '" << outDir << "' already exists" << std::endl;
      }
    }

    constexpr int TimeOutSec = 60; // Apis are slow; wait for a minute
    std::uniform_int_distribution<size_t> wordDist{0, N};
    auto sleepT = std::chrono::minutes(60) / imgsPerHour;
    httplib::Client client("https://api.openai.com");
    client.set_read_timeout(TimeOutSec, 0);
    client.set_write_timeout(TimeOutSec, 0);
    httplib::Headers headers = {{"Content-Type", "application/json"},
                                {"Authorization", "Bearer " + apiKey}};
    while (!interrupt_received) {
      try {
        const std::string prompt{std::string{"Simple pixel art of "} +
                                 std::string{HolidayWords[wordDist(rng_)]}};
        std::cout << "Generating image for '" << prompt << "'" << std::endl;

        std::string body = R"({
          "model": "dall-e-3",
          "prompt": ")" + prompt +
                           R"(",
          "n": 1,
          "size": "1024x1024"
        })";
        auto res = client.Post("/v1/images/generations", headers, body,
                               "application/json");
        if (!res) {
          std::ostringstream oss;
          oss << "HTTP error: '" << httplib::to_string(res.error()) << "'"
              << std::endl;
          throw std::runtime_error(oss.str());
        }
        if (res->status != httplib::StatusCode::OK_200) {
          std::ostringstream oss;
          oss << "Failed to talk to openai. Got Status: '" << res->status << "'"
              << std::endl;
          throw std::runtime_error(oss.str());
        }

        // Parse the JSON response
        try {
          std::cout << "Parsing response from openai" << std::endl;
          const auto &r = json::parse(res->body);
          if (r["data"].empty() || !r["data"][0].contains("url")) {
            std::ostringstream oss;
            oss << "Payload not in correct format. Does not have `data` "
                   "or `url`"
                << std::endl;
            throw std::runtime_error(oss.str());
          }

          std::string uri = r["data"][0]["url"];
          fs::path p = outDir / (normalize(prompt) + ".png");
          { // serialize to a file
            std::cout << "Downloading image to " << p << std::endl;

            std::string host, path;
            if (!parse_url(uri, host, path)) {
              std::ostringstream oss;
              oss << "Invalid URL '" << uri << "'" << std::endl;
              throw std::runtime_error(oss.str());
            }
            httplib::Client c(host);
            c.set_read_timeout(TimeOutSec, 0);
            c.set_write_timeout(TimeOutSec, 0);
            res = c.Get(path);
            if (!res) {
              std::ostringstream oss;
              oss << "Failed to download image from '" << uri
                  << "'. HTTP error: '" << httplib::to_string(res.error())
                  << "'" << std::endl;
              throw std::runtime_error(oss.str());
            }
            if (res->status != httplib::StatusCode::OK_200) {
              std::ostringstream oss;
              oss << "Failed to download image from '" << uri
                  << "'. Got Status: '" << res->status << "'" << std::endl;
              throw std::runtime_error(oss.str());
            }

            std::ofstream out(p, std::ios::binary);
            if (!out) {
              std::ostringstream oss;
              oss << "Could not open '" << p << "' for writing" << std::endl;
              throw std::runtime_error(oss.str());
            }
            out.write(res->body.data(), res->body.size());
            out.exceptions(out.failbit);
            out.close();
          }
          // parse into image
          auto img = image(p, targetWidth_, targetHeight_);
          if (!img) {
            std::ostringstream oss;
            oss << "Skipping '" << p << "' as it is empty" << std::endl;
            throw std::runtime_error(oss.str());
          }
          { // convert image to canvas
            std::scoped_lock l{genImgMutex_};
            genImgs_.emplace(canvas(*img, matrix_->CreateFrameCanvas()));
          }
        } catch (json::parse_error &e) {
          std::ostringstream oss;
          oss << "JSON parsing error in payload: " << e.what() << std::endl;
          throw std::runtime_error(oss.str());
        }
      } catch (const std::runtime_error &e) {
        // catch all errors so that we can sleep after this failed attempt
        std::cerr << e.what() << std::endl;
      }

      std::this_thread::sleep_for(sleepT);
    }

    if (!fs::remove_all(outDir)) {
      std::cerr << "Failed to remove '" << outDir << "'" << std::endl;
    }
  }

  std::string normalize(const std::string &filePath) {
    std::string r = filePath;
    std::replace(r.begin(), r.end(), ' ', '_');
    return r;
  }

  bool parse_url(const std::string &url, std::string &host, std::string &path) {
    const std::string protocol_delimiter = "://";
    size_t protocol_end = url.find(protocol_delimiter);
    if (protocol_end == std::string::npos) {
      return false; // Protocol not found
    }

    size_t host_start = protocol_end + protocol_delimiter.length();
    size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
      host = url;
      path = "/";
    } else {
      host = url.substr(0, path_start);
      path = url.substr(path_start);
    }

    return true;
  }

  std::shared_ptr<rgb_matrix::RGBMatrix> matrix_;
  std::vector<rgb_matrix::FrameCanvas *> imgs_;
  std::queue<rgb_matrix::FrameCanvas *> genImgs_;
  int targetWidth_;
  int targetHeight_;
  std::default_random_engine rng_;
  std::uniform_int_distribution<size_t> imgDist_;
  std::mutex genImgMutex_;
  std::thread genThrd_;

  static constexpr std::array<std::string_view, N> HolidayWords{
      "Santa Claus",
      "Snowman",
      "Reindeer",
      "Snow",
      "Bells",
      "Christmas Tree",
      "Ornaments",
      "Sleigh",
      "Presents",
      "Elves",
      "Stockings",
      "Mistletoe",
      "Holly",
      "Eggnog",
      "Tinsel",
      "Candy Cane",
      "Gingerbread",
      "Fireplace",
      "Garland",
      "Wreath",
      "Carolers",
      "Nutcracker",
      "Poinsettia",
      "Candle",
      "Star",
      "Angel",
      "Nativity",
      "Advent Calendar",
      "Yule Log",
      "Midnight Mass",
      "Snowflake",
      "Ice Skating",
      "Hot Chocolate",
      "Christmas Lights",
      "Jingle Bells",
      "Christmas Card",
      "Fruitcake",
      "Turkey",
      "Mince Pie",
      "Mulled Wine",
      "Ribbons",
      "Pinecone",
      "Gingerbread House",
      "Christmas Market",
      "Champagne",
      "Fireworks",
      "Countdown",
      "New Year's Eve",
      "Auld Lang Syne",
      "Party Hats",
      "Confetti",
      "Streamers",
      "Ball Drop",
      "Resolution",
      "Festoon",
      "Cranberry",
      "Holiday Parade",
      "Fairy Lights",
      "Ice Rink",
      "Holiday Music",
      "Plum Pudding",
      "Roast Beef",
      "Chestnuts",
      "Gravy",
      "Potato Latkes",
      "Dreidel",
      "Menorah",
      "Hanukkah Gelt",
      "Kwanzaa Candle",
      "Harvest",
      "Gift Exchange",
      "Winter Solstice",
      "Festival of Lights",
      "Skiing",
      "Sled",
      "Snow Boots",
      "Mittens",
      "Scarf",
      "Beanie",
      "Sweater",
      "Holiday Train",
      "Toy Soldier",
      "Lantern",
      "Figgy Pudding",
      "Wassail",
      "Gingerbread Latte",
      "Peppermint",
      "Holiday Movie",
      "Krampus",
      "Evergreen",
      "Holiday Bazaar",
      "Ugly Christmas Sweater",
      "Candy Apples",
      "Ginger Snap",
      "Sugar Plums",
      "Mall Santa",
      "Holiday Wishes",
      "Winter Wonderland",
      "Season's Greetings",
      "Toy Drive",
      "Snowball Fight",
      "Holiday Inn",
      "Gift Wrapping",
      "Holiday Sale",
  };
};

namespace {
void usage() {
  std::cerr << "Usage: yulegen [options]" << std::endl;
  std::cerr << "Display GenAI images on a RGB LED Matrix" << std::endl;
  std::cerr << "Options:" << std::endl;
  std::cerr << "\t --bootstrap-imgs-path : Directory to some filler "
               "images(default: "
               "bootstrap_imgs)";
  std::cerr << "\t --openai-api-key : Api key from openapi "
               "https://platform.openai.com/account/billing/overview(default: "
               "$OPENAI_API_KEY)"
            << std::endl;
  std::cerr << "\t --genimgs-per-hour : Number of images to request from "
               "openai per hour(default: 20)"
            << std::endl;
  std::cerr << "\t --animation-duration-ms : Time to wait before rendering the "
               "next image(default: 5s) "
            << std::endl;
  rgb_matrix::PrintMatrixFlags(stderr);
}

std::optional<std::string> parse(int argc, char *argv[],
                                 std::string &bootstrapImgsPath,
                                 std::string &openaiApiKey, int &imgsPerHour,
                                 std::chrono::milliseconds &sleep) {
  using namespace std::chrono;

  // defaults
  bootstrapImgsPath = "bootstrap_imgs/";
  {
    const char *k = std::getenv("OPENAI_API_KEY");
    openaiApiKey = (k) ? std::string(k) : "";
  }
  imgsPerHour = 20;
  sleep = std::chrono::duration_cast<milliseconds>(seconds(5));

  struct option long_options[] = {
      {"bootstrap-imgs-path", required_argument, 0, 'p'},
      {"openai-api-key", required_argument, 0, 'k'},
      {"genimgs-per-hour", required_argument, 0, 'i'},
      {"animation-duration-ms", required_argument, 0, 'd'},
      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "p:k:d:", long_options, NULL)) != -1) {
    switch (opt) {
    case 'p':
      bootstrapImgsPath = optarg;
      if (!fs::exists(bootstrapImgsPath)) {
        std::ostringstream oss;
        oss << "'" << bootstrapImgsPath << "' does not exist" << std::endl;
        return oss.str();
      }
      if (!fs::is_directory(bootstrapImgsPath)) {
        std::ostringstream oss;
        oss << "'" << bootstrapImgsPath << "' is not a directory" << std::endl;
        return oss.str();
      }
      break;
    case 'k':
      openaiApiKey = optarg;
      break;
    case 'i':
      imgsPerHour = std::atoi(optarg);
      break;
    case 'd':
      sleep = std::chrono::milliseconds(std::atoi(optarg));
      break;
    default:
      break;
    }
  }
  return std::nullopt;
}
} // namespace

int main(int argc, char *argv[]) {
  using rgb_matrix::RGBMatrix;

  // matrix options
  RGBMatrix::Options matrixOpts;
  matrixOpts.hardware_mapping = "adafruit-hat-pwm";
  matrixOpts.brightness = 20;
  matrixOpts.rows = 32;
  matrixOpts.cols = 32;
  matrixOpts.chain_length = 1;
  rgb_matrix::RuntimeOptions runtimeOpts;
  runtimeOpts.drop_priv_user = getenv("SUDO_UID");
  runtimeOpts.drop_priv_group = getenv("SUDO_GID");
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv, &matrixOpts,
                                         &runtimeOpts, true)) {
    usage();
    return 1;
  }

  // yulegen options
  std::string bootstrapImgsPath;
  std::string openaiApiKey;
  int imgsPerHour;
  std::chrono::milliseconds sleepT;
  if (auto r = parse(argc, argv, bootstrapImgsPath, openaiApiKey, imgsPerHour,
                     sleepT)) {
    // parse failed
    std::cerr << *r << std::endl;
    return 1;
  }

  // create matrix
  runtimeOpts.do_gpio_init = true;
  std::shared_ptr<RGBMatrix> matrix{
      RGBMatrix::CreateFromOptions(matrixOpts, runtimeOpts)};
  if (!matrix) {
    return 1;
  }

  // setup interrupt handlers. yulegen runs until interrupted
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  // main runloop
  auto gen =
      YuleGenerator<>::create(matrix, bootstrapImgsPath, matrixOpts.cols,
                              matrixOpts.rows, openaiApiKey, imgsPerHour);
  while (!interrupt_received) {
    auto canvas = gen.next();
    if (!canvas) {
      std::cout << "No image to display" << std::endl;
      continue;
    }
    matrix->SwapOnVSync(*canvas);
    std::this_thread::sleep_for(sleepT);
  }

  return 0;
}