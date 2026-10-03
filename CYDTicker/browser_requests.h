#pragma once
#include <Arduino.h>
static const char REQUEST_SCRIPT[] PROGMEM = R"JS(
<script>
async function requestJson(url,options){
 const deadline=Date.now()+90000;
 async function get(target,opts){const controller=new AbortController(),timer=setTimeout(function(){controller.abort();},15000);try{const response=await fetch(target,Object.assign({},opts||{},{signal:controller.signal}));const data=await response.json();return {response:response,data:data};}finally{clearTimeout(timer);}}
 let result=await get(url,options),job=result.response.status===202?result.data.job:null;
 while(job&&result.response.status===202){if(Date.now()>deadline)throw new Error('History request timed out. Retry when the device is available.');await new Promise(function(resolve){setTimeout(resolve,350);});result=await get('/api/history-job?id='+encodeURIComponent(job));}
 return result;
}
function transactionRequestId(){const bytes=new Uint8Array(16);crypto.getRandomValues(bytes);return Array.from(bytes,function(b){return b.toString(16).padStart(2,'0');}).join('');}
</script>
)JS";
