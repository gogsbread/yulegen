#include "matrix/include/led-matrix.h"

#include <getopt.h>

#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <vector>

#include <Magick++.h>

namespace fs = std::filesystem;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) { interrupt_received = true; }

class YuleGenerator {
public:
  static std::optional<YuleGenerator>
  create(std::shared_ptr<rgb_matrix::RGBMatrix> matrix,
         const fs::path bootstrapImgs, int width = 32, int height = 32) {
    if (!fs::is_directory(bootstrapImgs)) {
      return std::nullopt;
    }

    Magick::InitializeMagick(NULL);
    std::vector<rgb_matrix::FrameCanvas *> canvases;
    for (const auto &file : fs::directory_iterator(bootstrapImgs)) {
      if (!file.is_regular_file()) {
        std::cout << "Skipping '" << file.path()
                  << "' as it is not a regular file" << std::endl;
        continue;
      }
      auto img = image(file.path());
      if (!img) {
        std::cout << "Skipping '" << file.path() << "' as it is empty"
                  << std::endl;
        continue;
      }
      scale_image(*img, width, height);
      canvases.emplace_back(canvas(*img, matrix->CreateFrameCanvas()));
    }
    return YuleGenerator{std::move(canvases)};
  }

  std::optional<rgb_matrix::FrameCanvas *> next() {
    if (!genImgs_.empty()) {
      const auto &img = genImgs_.front();
      genImgs_.pop();
      return img;
    }
    if (!bootStrapImgs_.empty()) {
      std::mt19937 rng(std::random_device{}());
      std::uniform_int_distribution<std::size_t> dist(0, bootStrapImgs_.size() -
                                                             1);
      return bootStrapImgs_.at(dist(rng));
    }
    return std::nullopt;
  }

private:
  YuleGenerator(std::vector<rgb_matrix::FrameCanvas *> &&canvases)
      : bootStrapImgs_(std::move(canvases)) {}

  static std::optional<Magick::Image> image(fs::path p) {
    std::vector<Magick::Image> imgs{};
    Magick::readImages(&imgs, p);
    if (imgs.empty()) {
      return std::nullopt;
    }
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

  std::vector<rgb_matrix::FrameCanvas *> bootStrapImgs_;
  std::queue<rgb_matrix::FrameCanvas *> genImgs_;
};

namespace {
void usage() {
  std::cerr << "Usage: yulegen [options]" << std::endl;
  std::cerr << "Display GenAI images on a RGB LED Matrix" << std::endl;
  std::cerr << "Options:" << std::endl;
  std::cerr
      << "\t --bootstrap-imgs-path : Directory to some filler images(default: "
         "bootstrap_imgs)"
      << std::endl;
  rgb_matrix::PrintMatrixFlags(stderr);
}

std::optional<std::string> parse(int argc, char *argv[],
                                 std::string &bootstrap_imgs_path) {
  struct option long_options[] = {
      {"bootstrap-imgs-path", required_argument, 0, 'p'}, {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "p:", long_options, NULL)) != -1) {
    switch (opt) {
    case 'p':
      bootstrap_imgs_path = optarg;
      if (!fs::exists(bootstrap_imgs_path)) {
        std::ostringstream oss;
        oss << "'" << bootstrap_imgs_path << "' does not exist" << std::endl;
        return oss.str();
      }
      if (!fs::is_directory(bootstrap_imgs_path)) {
        std::ostringstream oss;
        oss << "'" << bootstrap_imgs_path << "' is not a directory"
            << std::endl;
        return oss.str();
      }
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
  matrixOpts.brightness = 50;
  matrixOpts.rows = 32;
  matrixOpts.chain_length = 1;
  rgb_matrix::RuntimeOptions runtimeOpts;
  // If started with 'sudo': make sure to drop privileges to same user
  // we started with, which is the most expected (and allows us to read
  // files as that user).
  // TODO: have to check if this is even used
  runtimeOpts.drop_priv_user = getenv("SUDO_UID");
  runtimeOpts.drop_priv_group = getenv("SUDO_GID");
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv, &matrixOpts,
                                         &runtimeOpts, true)) {
    usage();
    return 1;
  }

  // yulegen options
  std::string bootstrapImgsPath{
      "/home/antonyd/Projects/yulegen/bootstrap_imgs/"};
  if (auto r = parse(argc, argv, bootstrapImgsPath); r) {
    // parse failed
    std::cerr << *r << std::endl;
    return 1;
  }
  std::cout << bootstrapImgsPath << std::endl;

  // create matrix with options
  runtimeOpts.do_gpio_init = true;
  std::shared_ptr<RGBMatrix> matrix{
      RGBMatrix::CreateFromOptions(matrixOpts, runtimeOpts)};
  if (!matrix)
    return 1;

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  // main runloop
  auto gen = YuleGenerator::create(matrix, bootstrapImgsPath);
  if (gen) {
    for (size_t i{0}; i < 5; ++i) {
      auto canvas = gen->next();
      if (!canvas) {
        std::cout << "No image to display" << std::endl;
        continue;
      }
      matrix->SwapOnVSync(*canvas);
      sleep(5);
    }
  }

  return 0;
}