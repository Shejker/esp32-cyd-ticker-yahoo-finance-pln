#pragma once
#include "Arduino.h"
#include <map>
#include <algorithm>
#include <stdexcept>
constexpr int CONTENT_LENGTH_UNKNOWN=-1;
enum UploadStatus {UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload {UploadStatus status=UPLOAD_FILE_START;size_t currentSize=0,totalSize=0;uint8_t buf[2048]{};};
struct WebServer {
  std::string html;
  int status=200;
  size_t largestStringChunk=0,flashChunks=0;
  bool chunked=false,finished=false;
  HTTPUpload incoming;
  HTTPUpload& upload(){return incoming;}
  std::map<std::string,String> args;
  std::map<std::string,String> headers;
  String arg(const String& key)const{auto it=args.find(key.c_str());return it==args.end()?String():it->second;}
  bool hasArg(const String& key)const{return args.count(key.c_str());}
  void sendHeader(const char* key,const char* value){headers[key]=value;}
  void setContentLength(int length){chunked=length==CONTENT_LENGTH_UNKNOWN;finished=false;}
  void send(int code,const char*,const String& s){status=code;html=s.c_str();}
  void sendContent(const String& s){
    if(finished)throw std::runtime_error("Content sent after HTTP terminating chunk");
    largestStringChunk=std::max(largestStringChunk,s.length());html+=s.c_str();
    if(chunked&&s.isEmpty())finished=true;
  }
  void sendContent_P(const char* s){
    if(finished)throw std::runtime_error("Flash content sent after HTTP terminating chunk");
    ++flashChunks;html+=s;
  }
};
