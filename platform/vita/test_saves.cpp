#define __vita__ 1
#include "../../lib/N64ModernRuntime/librecomp/src/files.cpp"
#include <iostream>
#include <iterator>
int main(int argc, char **argv) {
    if(argc!=2) return 1;
    std::filesystem::path directory=argv[1];
    if(std::filesystem::exists(directory)) { std::cerr<<"Test directory must not exist\n"; return 1; }
    std::filesystem::create_directories(directory);
    const auto file=directory/"save.bin";
    auto read=[](const std::filesystem::path &path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    const std::string first(6001,'a'),second(5003,'b');
    for(const auto *contents:{&first,&second}) {
        { auto out=recomp::open_output_file_with_backup(file,std::ios::binary); out.write(contents->data(),contents->size()); }
        if(!recomp::finalize_output_file_with_backup(file) || read(file)!=*contents) return 2;
    }
    if(read(directory/"save.bin.bak")!=first || std::filesystem::exists(directory/"save.bin.temp")) return 3;
    if(recomp::finalize_output_file_with_backup(file) || read(file)!=second) return 4;
    std::filesystem::remove_all(directory);
    std::cout<<"Vita save copy: creation, overwrite, backup, partial final block and missing-temp preservation passed\n";
}
