#pragma once
#include <Arduino.h>

static const char TRACKER_SCRIPT[] PROGMEM = R"SCRIPT(
<script>
'use strict';
var config=JSON.parse(document.getElementById('tracker-config').textContent);
var T=config.tickers.slice(),txSeq=0,savingTransactions=false;
var el=function(id){return document.getElementById(id);};
var editVersion='';
if(config.storageWritable===false)el('manualGlobalStatus').textContent='Storage is unavailable. This is a read-only recovery view; download both JSON backups before changing the filesystem or partitions.';
else if(config.storageRecovered)el('manualGlobalStatus').textContent='A damaged storage snapshot was detected. The last valid backup was loaded; verify your latest transactions and account updates before saving again.';
el('settingsOpen').onclick=function(){el('settingsDialog').showModal();};
el('settingsClose').onclick=function(){el('settingsDialog').close();};
function renderTickers(){
  el('tl').replaceChildren();
  T.forEach(function(t,i){
    var row=document.createElement('div');row.className='trow';
    var number=document.createElement('span');number.className='tidx';number.textContent=i+1;
    var input=document.createElement('input');input.className='inp';input.value=t;input.placeholder='e.g. AAPL';input.setAttribute('aria-label','Ticker '+(i+1));
    input.oninput=function(){T[i]=this.value.toUpperCase();this.value=T[i];};
    var remove=document.createElement('button');remove.type='button';remove.className='rm';remove.textContent='×';remove.setAttribute('aria-label','Remove ticker '+t);
    remove.onclick=function(){T.splice(i,1);renderTickers();};row.append(number,input,remove);el('tl').appendChild(row);
  });
  el('tickerAdd').disabled=T.length>=8;
}
function addT(){if(T.length>=8)return;T.push('');renderTickers();el('tl').lastElementChild.querySelector('input').focus();}
el('cfgform').addEventListener('submit',function(event){
  var names=T.map(function(t){return t.trim().toUpperCase();}).filter(Boolean);
  var removed=config.tickers.filter(function(t){return names.indexOf(t)<0&&config.lotCounts[t]>0;});
  if(removed.length&&!confirm('Removing '+removed.join(', ')+' also deletes its transactions from the device. Download a backup first. Continue?')){event.preventDefault();return;}
  el('confirmRemove').value=removed.length?'1':'0';el('th').value=names.join(',');
});renderTickers();
var ranges=[['1d','1D'],['5d','5D'],['1mo','1M'],['3mo','3M'],['6mo','6M'],['ytd','YTD'],['1y','1Y'],['3y','3Y'],['max','MAX']];
ranges.forEach(function(r){var b=document.createElement('button');b.type='button';b.className='rb'+(r[0]===config.range?' ra':'');b.textContent=r[1];b.setAttribute('aria-pressed',String(r[0]===config.range));b.onclick=function(){el('ri').value=r[0];el('rg').querySelectorAll('button').forEach(function(other){var active=other===b;other.classList.toggle('ra',active);other.setAttribute('aria-pressed',String(active));});};el('rg').appendChild(b);});
function manualToday(){var parts={};new Intl.DateTimeFormat('en-CA',{timeZone:'Europe/Warsaw',year:'numeric',month:'2-digit',day:'2-digit'}).formatToParts(new Date()).forEach(function(p){parts[p.type]=p.value;});return parts.year+'-'+parts.month+'-'+parts.day;}
function addTxRow(focus){
  var idx=txSeq++,wrap=document.createElement('div');wrap.innerHTML=el('txRowTpl').innerHTML.split('__IDX__').join(idx);var group=wrap.firstElementChild;
  el('txRows').appendChild(group);group.querySelectorAll('[type=date]').forEach(function(input){input.max=manualToday();});updateRemoveButtons();if(focus!==false)el('txSym'+idx).focus();return idx;
}
function removeTxRow(idx){if(savingTransactions)return;var row=document.querySelector('.trow-group[data-idx="'+idx+'"]');if(row)row.remove();if(!el('txRows').children.length)addTxRow(false);updateRemoveButtons();}
function updateRemoveButtons(){var groups=el('txRows').querySelectorAll('.trow-group');groups.forEach(function(g){el('txRmBtn'+g.dataset.idx).hidden=groups.length<=1;});}
async function fetchHistPrice(idx){
  var date=el('txDate'+idx),status=el('txRowStatus'+idx),button=el('txFetch'+idx);if(!date.value||!date.reportValidity()){status.textContent='Pick a valid date first.';return;}
  var symbol=el('txSym'+idx).value,day=date.value,price=el('txPrice'+idx),originalPrice=price.value;
  status.textContent='Fetching…';button.disabled=true;
  try{var result=await requestJson('/api/histprice?lt='+encodeURIComponent(symbol)+'&ld='+encodeURIComponent(day)),response=result.response,data=result.data;if(!response.ok||!data.ok||!Number.isFinite(data.pricePLN))throw new Error();if(!price.isConnected||savingTransactions)return;if(el('txSym'+idx).value!==symbol||date.value!==day||price.value!==originalPrice){status.textContent='Inputs changed; old quote was discarded. Fetch again if needed.';return;}price.value=data.pricePLN.toFixed(4);status.textContent='Loaded closing price. Check it against your broker statement.';}
  catch(error){if(status.isConnected)status.textContent='No historical quote. Enter the price manually.';}finally{if(button.isConnected)button.disabled=savingTransactions;}
}
function setSaving(busy){savingTransactions=busy;el('transactions').querySelectorAll('button,input,select').forEach(function(input){input.disabled=busy;});}
async function submitTx(){
  if(savingTransactions)return;var editing=el('txLotIdx').value!=='',rows=[],invalid=false,status=el('fetchStatus');
  el('txRows').querySelectorAll('.trow-group').forEach(function(group){
    var idx=group.dataset.idx,date=el('txDate'+idx),qty=el('txQty'+idx),price=el('txPrice'+idx);
    if(!date.value&&!qty.value&&!price.value)return;
    if(![date,qty,price].every(function(input){return input.reportValidity();})||Number(qty.value)===0){invalid=true;return;}
    rows.push({group:group,idx:idx,sym:el('txSym'+idx).value,date:date.value,qty:qty.value,price:price.value});
  });
  if(invalid){status.textContent='Fill in all fields. Quantity must not be zero.';return;}
  if(!rows.length){status.textContent='Add at least one transaction.';return;}
  setSaving(true);var saved=0,failed=0;
  for(var row of rows){
    var body=new URLSearchParams({lt:row.sym,ld:row.date,lq:row.qty,lp:row.price,format:'json'});
    if(editing){body.set('lk',el('txLotIdx').value);body.set('lo',el('txLotOrigSym').value);body.set('lv',editVersion);}
    var signature=body.toString();
    if(row.group.dataset.uncertain==='true'&&signature!==row.group.dataset.signature){failed++;el('txRowStatus'+row.idx).textContent='The previous response was lost. Refresh and check saved transactions before changing this row.';continue;}
    if(signature!==row.group.dataset.signature){row.group.dataset.signature=signature;row.group.dataset.requestId=transactionRequestId();}
    body.set('rid',row.group.dataset.requestId);body.set('base',config.revision||0);
    var uncertain=true;
    try{var result=await requestJson(editing?'/editlot':'/addlot',{method:'POST',body:body}),response=result.response,data=result.data;uncertain=false;if(!response.ok||!data.ok)throw new Error(data.error||'Could not save');config.revision=data.revision===undefined?config.revision:data.revision;saved++;if(!editing)row.group.remove();}
    catch(error){failed++;row.group.dataset.uncertain=String(uncertain);el('txRowStatus'+row.idx).textContent=error.message+(uncertain?' Response uncertain: retry unchanged or refresh and check the transaction list.':'');if(uncertain)break;}
  }
  setSaving(false);
  if(failed){if(!el('txRows').children.length)addTxRow(false);updateRemoveButtons();status.textContent=el('txRows').querySelector('[data-uncertain="true"]')?'A response was lost. Remaining rows may already be saved. Retry unchanged, or refresh and check before editing.':saved+' saved, '+failed+' failed. Only unsaved rows remain; correct them before retrying. Existing totals update after reloading.';}
  else{status.textContent=saved+' saved. Reloading…';location.assign('/');}
}
function editLot(button){
  if(savingTransactions)return;var row=button.dataset;el('txRows').replaceChildren();var idx=addTxRow(false);
  editVersion=row.version||'';
  el('txSym'+idx).value=row.sym;el('txDate'+idx).value=row.date;el('txQty'+idx).value=row.qty;el('txPrice'+idx).value=row.price;
  el('txLotIdx').value=row.lot;el('txLotOrigSym').value=row.sym;el('txAddBtn').hidden=true;el('txSubmitBtn').textContent='Update Transaction';el('txCancelBtn').hidden=false;el('fetchStatus').textContent='Editing a saved transaction.';el('txRows').scrollIntoView({behavior:'smooth',block:'center'});
}
function cancelEdit(){if(savingTransactions)return;el('txLotIdx').value='';el('txLotOrigSym').value='';el('txRows').replaceChildren();addTxRow(false);el('txAddBtn').hidden=false;el('txSubmitBtn').textContent='+ Add Transaction';el('txCancelBtn').hidden=true;el('fetchStatus').textContent='';}
async function deleteLot(button){
  if(savingTransactions||!confirm('Delete this transaction?'))return;setSaving(true);button.dataset.requestId=button.dataset.requestId||transactionRequestId();
  try{var result=await requestJson('/dellot',{method:'POST',body:new URLSearchParams({lt:button.dataset.sym,lk:button.dataset.lot,lv:button.dataset.version||'',rid:button.dataset.requestId,base:config.revision||0,format:'json'})}),response=result.response,data=result.data;if(!response.ok||!data.ok)throw new Error(data.error||'Could not delete');location.reload();}
  catch(error){el('fetchStatus').textContent=error.message+' Refresh and check the list if the response was lost.';setSaving(false);}
}
addTxRow(false);
document.querySelectorAll('[data-tracker-save]').forEach(function(button){button.disabled=false;});
</script></body></html>
)SCRIPT";
