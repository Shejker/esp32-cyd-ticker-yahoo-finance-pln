#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
#include <vector>
#include <cstring>

struct FakeFilesystem;
extern FakeFilesystem LittleFS;
class File {
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t position=0;
public:
  File()=default;
  explicit File(std::shared_ptr<std::vector<uint8_t>> data):bytes(data){}
  explicit operator bool()const{return !!bytes;}
  size_t size()const{return bytes?bytes->size():0;}
  bool seek(size_t n){position=n;return bytes&&n<=bytes->size();}
  int read(){return bytes&&position<bytes->size()?(*bytes)[position++]:-1;}
  size_t read(uint8_t* out,size_t n){if(!bytes)return 0;n=std::min(n,bytes->size()-std::min(position,bytes->size()));std::memcpy(out,bytes->data()+position,n);position+=n;return n;}
  size_t readBytes(char* out,size_t n){return read(reinterpret_cast<uint8_t*>(out),n);}
  size_t write(const uint8_t* data,size_t n);
  size_t write(uint8_t byte){return write(&byte,1);}
  void flush(){} void close(){bytes.reset();}
};
struct FakeFilesystem {
  std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
  bool mounted=true,failWrite=false,failRename=false;int formats=0;
  bool begin(bool){return mounted;}
  bool format(){++formats;mounted=true;files.clear();return true;}
  bool exists(const char* name){return files.count(name);}
  bool remove(const char* name){return files.erase(name)>0;}
  File open(const char* name,const char* mode){
    if(!mounted)return {};
    if(std::strcmp(mode,"w")==0)files[name]=std::make_shared<std::vector<uint8_t>>();
    if(!exists(name))return {};
    return File(files.at(name));
  }
  bool rename(const char* from,const char* to){if(failRename||!exists(from))return false;files[to]=files[from];files.erase(from);return true;}
};
inline FakeFilesystem LittleFS;
inline size_t File::write(const uint8_t* data,size_t n){
  if(!bytes||LittleFS.failWrite)return 0;
  if(bytes->size()<position+n)bytes->resize(position+n);
  std::memcpy(bytes->data()+position,data,n);position+=n;return n;
}
