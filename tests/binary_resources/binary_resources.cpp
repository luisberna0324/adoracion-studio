#include <BinaryData.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    };
    require(argc - 1 == BinaryData::namedResourceListSize, "Resource count differs from shared manifest");
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const std::string path(argv[i + 1]);
        const auto filename = path.substr(path.find_last_of("/\\") + 1);
        std::ifstream stream(path, std::ios::binary);
        require(stream.is_open(), "Cannot open " + path);
        const std::vector<char> expected((std::istreambuf_iterator<char>(stream)), {});
        const auto* name = BinaryData::namedResourceList[i];
        int size = -1;
        const auto* actual = BinaryData::getNamedResource(name, size);
        require(actual != nullptr && size == static_cast<int>(expected.size()), "Wrong size for " + filename);
        require(expected.empty() || std::memcmp(actual, expected.data(), expected.size()) == 0, "Wrong bytes for " + filename);
        require(actual[size] == 0 && actual[size + 1] == 0, "Missing padding for " + filename);
        require(filename == BinaryData::originalFilenames[i], "Wrong filename table for " + filename);
        const auto* original = BinaryData::getNamedResourceOriginalFilename(name);
        require(original != nullptr && filename == original, "Wrong filename lookup for " + filename);
    }
    int size = -1;
    require(BinaryData::getNamedResource("googleg_png", size) == BinaryData::googleg_png
                && size == BinaryData::googleg_pngSize && size > 0,
            "Google sign-in icon unavailable");
    require(BinaryData::getNamedResource("LuzDeEsperanza_pwproj", size) == BinaryData::LuzDeEsperanza_pwproj
                && size == BinaryData::LuzDeEsperanza_pwprojSize && size > 0,
            "Bundled demo unavailable");
    for (const auto* missing : { static_cast<const char*>(nullptr), "", "missing_resource", "googleg_png_typo" })
    {
        size = -1;
        require(BinaryData::getNamedResource(missing, size) == nullptr && size == 0, "Invalid resource lookup");
        require(BinaryData::getNamedResourceOriginalFilename(missing) == nullptr, "Invalid filename lookup");
    }
    std::cout << "Verified " << BinaryData::namedResourceListSize << " resources byte-for-byte, including Google icon and demo\n";
}
