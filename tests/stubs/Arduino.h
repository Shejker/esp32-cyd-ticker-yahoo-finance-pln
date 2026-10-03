#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#define PROGMEM
#define F(text) text
#define FPSTR(text) text

// Only the presentation layer is compiled with these host-test stand-ins.
class String {
  std::string value;
public:
  String() = default;
  String(const char* s):value(s?s:""){}
  String(const std::string& s):value(s){}
  String(int n):value(std::to_string(n)){}
  String(long n):value(std::to_string(n)){}
  String(unsigned int n):value(std::to_string(n)){}
  String(unsigned long n):value(std::to_string(n)){}
  String(long long n):value(std::to_string(n)){}
  String(double n, int decimals) {std::ostringstream s;s<<std::fixed<<std::setprecision(decimals)<<n;value=s.str();}
  String& operator+=(const String& other){value+=other.value;return *this;}
  friend String operator+(String a,const String& b){return a+=b;}
  bool operator==(const String& other)const{return value==other.value;}
  bool operator!=(const String& other)const{return value!=other.value;}
  bool equalsIgnoreCase(const String& other)const{
    if(value.size()!=other.value.size())return false;
    for(size_t i=0;i<value.size();++i)if(std::tolower((unsigned char)value[i])!=std::tolower((unsigned char)other.value[i]))return false;
    return true;
  }
  bool isEmpty()const{return value.empty();}
  size_t length()const{return value.size();}
  char operator[](size_t i)const{return value[i];}
  String substring(size_t start,size_t end)const{return value.substr(start,end-start);}
  String substring(size_t start)const{return value.substr(start);}
  bool endsWith(const String& suffix)const{return value.size()>=suffix.length()&&value.compare(value.size()-suffix.length(),suffix.length(),suffix.c_str())==0;}
  int indexOf(char c,size_t from=0)const{auto p=value.find(c,from);return p==std::string::npos?-1:(int)p;}
  int indexOf(const String& s)const{auto p=value.find(s.c_str());return p==std::string::npos?-1:(int)p;}
  long toInt()const{return std::strtol(value.c_str(),nullptr,10);}
  float toFloat()const{return std::strtof(value.c_str(),nullptr);}
  size_t write(uint8_t c){value+=static_cast<char>(c);return 1;}
  size_t write(const uint8_t* s,size_t n){value.append(reinterpret_cast<const char*>(s),n);return n;}
  void trim(){auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos){value.clear();return;}value=value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);}
  void toUpperCase(){for(char& c:value)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));}
  const char* c_str()const{return value.c_str();}
};
