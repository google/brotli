#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "./deorummolae.h"
#include "./sieve.h"

#define METHOD_DM 0
#define METHOD_SIEVE 1

size_t readInt(const char* str) {
  size_t result = 0;
  if (str[0] == 0 || str[0] == '0') {
    return 0;
  }
  for (size_t i = 0; i < 13; ++i) {
    if (str[i] == 0) {
      return result;
    }
    if (str[i] == 'k' || str[i] == 'K') {
      if ((str[i + 1] == 0) && ((result << 10) > result)) {
        return result << 10;
      }
      return 0;
    }
    if (str[i] == 'm' || str[i] == 'M') {
      if ((str[i + 1] == 0) && ((result << 20) > result)) {
        return result << 20;
      }
      return 0;
    }
    if (str[i] < '0' || str[i] > '9') {
      return 0;
    }
    size_t next = (10 * result) + (str[i] - '0');
    if (next <= result) {
      return 0;
    }
    result = next;
  }
  return 0;
}

static std::string readFile(const std::string& path) {
  std::ifstream file(path);
  std::string content(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return content;
}

static void writeFile(const char* file, const std::string& content) {
  std::ofstream outfile(file, std::ofstream::binary);
  outfile.write(content.c_str(), content.size());
  outfile.close();
}

/* Returns "base file name" or its tail, if it contains '/' or '\'. */
static const char* fileName(const char* path) {
  const char* separator_position = strrchr(path, '/');
  if (separator_position) path = separator_position + 1;
  separator_position = strrchr(path, '\\');
  if (separator_position) path = separator_position + 1;
  return path;
}

static void printHelp(const char* name) {
  fprintf(stderr, "Usage: %s [OPTION]... DICTIONARY [SAMPLE]...\n", name);
  fprintf(stderr,
      "Options:\n"
      "  --dm     use 'deorummolae' engine\n"
      "  --sieve  use 'sieve' engine (default)\n"
      "  -t#      set target dictionary size (limit); default: 16K\n"
      "  -s#      set slize length for 'sieve'; default: 33\n"
      "# is a decimal number with optional k/K/m/M suffix.\n\n");
}

int main(int argc, char const* argv[]) {
  int dictionaryArg = -1;
  int method = METHOD_SIEVE;
  int sieveSliceLen = 33;
  int targetSize = 16 << 10;

  std::vector<uint8_t> data;
  std::vector<size_t> sizes;
  size_t total = 0;
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }
    if (argv[i][0] == '-') {
      if (argv[i][1] == '-') {
        if (std::strcmp("--sieve", argv[i]) == 0) {
          method = METHOD_SIEVE;
          continue;
        }
        if (std::strcmp("--dm", argv[i]) == 0) {
          method = METHOD_DM;
          continue;
        }
        printHelp(fileName(argv[0]));
        fprintf(stderr, "Invalid option '%s'\n", argv[i]);
        exit(1);
      }
      if (argv[i][1] == 's') {
        sieveSliceLen = readInt(&argv[i][2]);
        if (sieveSliceLen < 4 || sieveSliceLen > 256) {
          printHelp(fileName(argv[0]));
          fprintf(stderr, "Invalid option '%s'\n", argv[i]);
          exit(1);
        }
      } else if (argv[i][1] == 't') {
        targetSize = readInt(&argv[i][2]);
        if (targetSize < 256 || targetSize > (1 << 25)) {
          printHelp(fileName(argv[0]));
          fprintf(stderr, "Invalid option '%s'\n", argv[i]);
          exit(1);
        }
      } else {
        printHelp(fileName(argv[0]));
        fprintf(stderr, "Unrecognized option '%s'\n", argv[i]);
        exit(1);
      }
      continue;
    }
    if (dictionaryArg == -1) {
      dictionaryArg = i;
      continue;
    }
    std::string content = readFile(argv[i]);
    data.insert(data.end(), content.begin(), content.end());
    total += content.size();
    sizes.push_back(content.size());
  }
  if (dictionaryArg == -1 || total == 0) {
    printHelp(fileName(argv[0]));
    fprintf(stderr, "Not enough arguments\n");
    exit(1);
  }

  if (method == METHOD_SIEVE) {
    writeFile(argv[dictionaryArg],
        sieve_generate(targetSize, sieveSliceLen, sizes, data.data()));
  } else if (method == METHOD_DM) {
    writeFile(argv[dictionaryArg],
        DM_generate(targetSize, sizes, data.data()));
  } else {
    printHelp(fileName(argv[0]));
    fprintf(stderr, "Unknown generator\n");
    exit(1);
  }
  return 0;
}
