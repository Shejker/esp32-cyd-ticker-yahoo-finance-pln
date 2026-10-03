// Compile the actual Yahoo reader with ESP32-like non-blocking TLS semantics:
// NetworkClientSecure::read returns -1 when the next packet has not arrived yet.
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../CYDTicker/http_body_reader.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <vector>
using std::min;

static uint32_t clockMs=0;
unsigned long millis(){return clockMs;}
void delay(unsigned long ms){clockMs+=(uint32_t)ms;}
struct Packet {size_t end;uint32_t arrival;};
struct NetworkClient {
  std::string bytes;
  std::vector<Packet> packets;
  size_t cursor=0;
  unsigned long timeout=40;
  bool closeAtEnd=true;
  int reads=0;
  int available(){
    for(const auto& packet:packets)if(cursor<packet.end)
      return (int32_t)(clockMs-packet.arrival)>=0?(int)(packet.end-cursor):0;
    return 0;
  }
  bool connected(){return cursor<bytes.size()||!closeAtEnd;}
  unsigned long getTimeout(){return timeout;}
  int read(uint8_t* out,size_t count){
    ++reads;int availableNow=available();
    if(availableNow<=0)return -1; // This is a temporary gap, not necessarily EOF.
    size_t n=min(count,(size_t)availableNow);std::memcpy(out,bytes.data()+cursor,n);cursor+=n;return (int)n;
  }
  // Model the core's inherited readBytes: a negative TLS read terminates early.
  size_t readBytes(uint8_t* out,size_t count){int n=read(out,count);return n>0?(size_t)n:0;}
};
using Stream=NetworkClient;
struct HTTPClient {
  NetworkClient stream;
  String transferEncoding;
  NetworkClient& getStream(){return stream;}
  String header(const char*){return transferEncoding;}
};
#include "yahoo-reader-under-test.h"

std::string chunk(const std::string& part){char size[32];snprintf(size,sizeof(size),"%zx;test=yes\r\n",part.size());return std::string(size)+part+"\r\n";}
HTTPClient response(const std::string& json,bool chunked,size_t packetSize=13){
  clockMs=0;HTTPClient http;
  http.transferEncoding=chunked?"chunked":"";
  if(chunked){for(size_t i=0;i<json.size();i+=11)http.stream.bytes+=chunk(json.substr(i,11));http.stream.bytes+="0\r\n\r\n";}
  else http.stream.bytes=json;
  for(size_t end=packetSize;end<http.stream.bytes.size();end+=packetSize)http.stream.packets.push_back({end,(uint32_t)(http.stream.packets.size()*6)});
  http.stream.packets.push_back({http.stream.bytes.size(),(uint32_t)(http.stream.packets.size()*6)});
  return http;
}
JsonDocument filter(){JsonDocument f;f["chart"]["result"][0]["meta"]=true;f["chart"]["result"][0]["timestamp"]=true;f["chart"]["result"][0]["indicators"]["quote"][0]["close"]=true;return f;}
int main(int argc,char**){
  const std::string json=argc>1?std::string(std::istreambuf_iterator<char>(std::cin),{}):R"JSON({"chart":{"result":[{"meta":{"currency":"USD","regularMarketPrice":100,"regularMarketTime":1790955372},"timestamp":[1767312000,1767398400],"indicators":{"quote":[{"close":[99,100],"volume":[10,20]}]}}],"error":null}})JSON";
  JsonDocument f=filter(),doc;
  for(bool chunks:{false,true}){
    auto http=response(json,chunks);
    auto error=readYahooJson(http,doc,f);
    if(error)fprintf(stderr,"Delayed %s response failed: %s\n",chunks?"chunked":"identity",error.c_str());
    assert(!error);
    assert(!doc["chart"]["result"][0]["meta"].isNull());
    assert(doc["chart"]["result"][0]["meta"]["regularMarketPrice"].as<double>()>0);
    assert(doc["chart"]["result"][0]["indicators"]["quote"][0]["volume"].isNull());
  }
  if(argc>1){puts("PASS: real Yahoo response with delayed identity/chunked TLS packets.");return 0;}
  auto initialGap=response(json,false);
  for(auto& packet:initialGap.stream.packets)packet.arrival+=8;
  assert(!readYahooJson(initialGap,doc,f));
  // Truncation and a permanently stalled connection must still fail promptly.
  auto truncated=response(json.substr(0,json.size()/2),false);
  assert(readYahooJson(truncated,doc,f)==DeserializationError::IncompleteInput);
  auto stalled=response(json,false);stalled.stream.packets={{10,0},{json.size(),1000}};
  assert(readYahooJson(stalled,doc,f)==DeserializationError::IncompleteInput);assert(clockMs<=42);
  // A final delayed chunk/CRLF must not be mistaken for EOF.
  auto tail=response(json,true,1);assert(!readYahooJson(tail,doc,f));
  // Millisecond wraparound must not make the read timeout fire immediately.
  auto wrap=response(json,false,7);clockMs=UINT32_MAX-20;
  for(auto& packet:wrap.stream.packets)packet.arrival+=clockMs;
  assert(!readYahooJson(wrap,doc,f));
  // Buffered reads should consume a contiguous response in blocks, not 1 call/byte.
  auto buffered=response(json,false,json.size());buffered.stream.packets[0].arrival=0;
  assert(!readYahooJson(buffered,doc,f));assert(buffered.stream.reads<10);
  // Frequent tiny packets must not reset an unlimited total response timer.
  auto trickle=response(json,false,1);HttpSource bounded(trickle.stream,25);
  HttpBodyReader<HttpSource> body(bounded,false);
  assert(deserializeJson(doc,body,DeserializationOption::Filter(f))==DeserializationError::IncompleteInput);
  assert(clockMs>=25 && clockMs<=27);
  auto trickleWrap=response(json,false,1);clockMs=UINT32_MAX-10;
  for(auto& packet:trickleWrap.stream.packets)packet.arrival+=clockMs;
  uint32_t start=clockMs;HttpSource wrapBounded(trickleWrap.stream,25);HttpBodyReader<HttpSource> wrapBody(wrapBounded,false);
  assert(deserializeJson(doc,wrapBody,DeserializationOption::Filter(f))==DeserializationError::IncompleteInput);
  assert((uint32_t)(clockMs-start)>=25 && (uint32_t)(clockMs-start)<=27);
  puts("PASS: actual Yahoo reader handles delayed TLS packets, chunk boundaries, buffering, disconnects, timeouts and timer wraparound.");
}
