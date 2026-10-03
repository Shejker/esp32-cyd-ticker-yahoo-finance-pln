#pragma once
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cerrno>
#include <cstring>

// ArduinoJson's floating-point parser can shift tiny quantities by one ULP.
// Keep original numeric tokens so strtof and the exact-cent parser see the
// exported text. Prefix original strings/keys as S:, and numeric tokens as
// N:, so a JSON string can never masquerade as a numeric field. Streaming
// requires only a small token buffer, not another copy of the upload.
struct BackupRowReader {
  File& file;
  int first;
  bool invalid = false, inString = false, escaped = false;
  char pending[72]{};
  size_t pos = 0, length = 0;
  int raw() { if (first >= 0) { int c=first; first=-1; return c; } return file.read(); }
  static bool validNumber(const char* token) {
    const char* s=token;
    if (*s=='-') ++s;
    if (*s=='0') ++s;
    else { if (*s<'1'||*s>'9') return false; while (*s>='0'&&*s<='9') ++s; }
    if (*s=='.') { ++s; if (*s<'0'||*s>'9') return false; while (*s>='0'&&*s<='9') ++s; }
    if (*s=='e'||*s=='E') { ++s; if (*s=='+'||*s=='-') ++s; if (*s<'0'||*s>'9') return false; while (*s>='0'&&*s<='9') ++s; }
    return *s==0;
  }
  int read() {
    if(pos<length)return (unsigned char)pending[pos++];
    int c=raw();
    if(c<0)return c;
    if(inString){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')inString=false;return c;}
    if(c=='"'){inString=true;pending[0]='S';pending[1]=':';pos=0;length=2;return c;}
    if(c=='-'||(c>='0'&&c<='9')){
      char token[64]{};size_t n=0;
      do { if(n<sizeof(token)-1)token[n++]=(char)c;else invalid=true;c=raw(); }
      while(c=='-'||c=='+'||c=='.'||c=='e'||c=='E'||(c>='0'&&c<='9'));
      first=c;
      if(!validNumber(token))invalid=true;
      pending[0]='"';pending[1]='N';pending[2]=':';std::memcpy(pending+3,token,n);pending[n+3]='"';pos=1;length=n+4;return pending[0];
    }
    return c;
  }
  size_t readBytes(char* data,size_t size){size_t n=0;int c;while(n<size&&(c=read())>=0)data[n++]=(char)c;return n;}
};

inline const char* backupNumericToken(JsonVariantConst value) {
  const char* text=value.as<const char*>();
  return text&&std::strncmp(text,"N:",2)==0 ? text+2 : nullptr;
}
inline bool backupText(JsonVariantConst value,String& result) {
  const char* text=value.as<const char*>();
  if(!text||std::strncmp(text,"S:",2)!=0)return false;
  result=String(text+2);return true;
}
inline bool backupInteger(JsonVariantConst value,int64_t& result) {
  const char* text=backupNumericToken(value);if(!text)return false;
  const char* s=text;if(*s=='-')++s;if(!*s)return false;
  while(*s){if(*s<'0'||*s>'9')return false;++s;}
  char* end=nullptr;errno=0;long long number=std::strtoll(text,&end,10);
  if(errno==ERANGE||!end||*end)return false;result=number;return true;
}
