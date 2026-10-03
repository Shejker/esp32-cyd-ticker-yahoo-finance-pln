#include <algorithm>
#include <cassert>
#include <cstdio>
#include "../CYDTicker/persistent_store.h"
#include "../CYDTicker/investment_math.h"
#include "../CYDTicker/http_body_reader.h"

struct Bytes {std::string text;size_t at=0;int read(){return at<text.size()?(unsigned char)text[at++]:-1;}};
int main(){
  PersistentStore store;assert(store.begin());JsonDocument doc;doc["balanceCents"]=7617142;
  assert(store.save(doc)&&store.revision()==1);doc["balanceCents"]=7617242;
  assert(store.save(doc)&&store.revision()==2);
  PersistentStore reboot;JsonDocument loaded;assert(reboot.begin()&&reboot.load(loaded)&&loaded["balanceCents"]==7617242);
  LittleFS.failWrite=true;doc["balanceCents"]=999;assert(!reboot.save(doc)&&reboot.revision()==2);LittleFS.failWrite=false;
  LittleFS.failRename=true;assert(!reboot.save(doc)&&reboot.revision()==2);LittleFS.failRename=false;
  PersistentStore rebootAfterFailure;assert(rebootAfterFailure.begin()&&rebootAfterFailure.load(loaded)&&loaded["balanceCents"]==7617242);
  LittleFS.files.at("/portfolio-b.dat")->back()^=1;
  PersistentStore recovered;assert(recovered.begin()&&recovered.load(loaded)&&loaded["balanceCents"]==7617142&&recovered.recovered());
  // A complete maximum-sized portfolio no longer depends on 20 KB NVS.
  doc.clear();for(int i=0;i<480;++i){auto row=doc["lots"].add<JsonArray>();row.add(1790935200);char tiny[32];formatQuantity(1e-10f,tiny);row.add(serialized(String(tiny)));row.add(326656.44);row.add(i+1);}
  for(int i=0;i<240;++i){auto row=doc["history"].add<JsonArray>();row.add(1790935200);row.add(7617142);row.add(126277);}
  assert(measureJson(doc)>20480&&recovered.save(doc));
  PersistentStore maximum;assert(maximum.begin()&&maximum.load(loaded));assert(loaded["lots"][0][1].as<float>()==1e-10f);
  LittleFS.files.at("/portfolio-a.dat")->back()^=1;LittleFS.files.at("/portfolio-b.dat")->back()^=1;
  PersistentStore broken;assert(broken.begin()&&!broken.load(loaded)&&!broken.writable()&&!broken.save(doc));
  LittleFS.mounted=false;partitionBlank=false;int formats=LittleFS.formats;PersistentStore nonblank;
  assert(!nonblank.begin()&&LittleFS.formats==formats);partitionBlank=true;PersistentStore blank;assert(blank.begin()&&LittleFS.formats==formats+1);
  Bytes chunks{"7;test=yes\r\n{\"a\":1}\r\n0\r\n\r\n"};HttpBodyReader<Bytes> reader(chunks,true);JsonDocument json;
  assert(!deserializeJson(json,reader)&&json["a"]==1);
  Bytes fragmented{"3\r\n{\"a\r\n4\r\n\":2}\r\n0\r\n\r\n"};HttpBodyReader<Bytes> partial(fragmented,true);assert(!deserializeJson(json,partial)&&json["a"]==2);
  Bytes malformed{"bogus\r\n{}"};HttpBodyReader<Bytes> bad(malformed,true);assert(deserializeJson(json,bad));
  puts("PASS: actual A/B storage, reboot, failed/partial writes, rename failure, CRC recovery, maximum history, tiny quantities, safe blank-only formatting and chunked JSON streaming.");
}
