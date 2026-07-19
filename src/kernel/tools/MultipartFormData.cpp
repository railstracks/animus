#include "animus_kernel/tools/MultipartFormData.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace animus::kernel {

void MultipartFormData::AddFileFromPath(const std::string& name,
                                         const std::string& filepath,
                                         const std::string& contentType) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "[multipart] Cannot open file: " << filepath << std::endl;
        return;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string data = ss.str();

    // Extract filename from path
    std::string filename = filepath;
    auto pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) {
        filename = filename.substr(pos + 1);
    }

    AddFile(name, filename, contentType, data);
}

} // namespace animus::kernel