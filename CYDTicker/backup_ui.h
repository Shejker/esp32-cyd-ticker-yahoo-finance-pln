#pragma once
#include <Arduino.h>

static const char BACKUP_CONTROLS[] PROGMEM = R"BACKUP(
<section class="backup-controls" aria-labelledby="backupTitle"><h3 id="backupTitle">Backups</h3>
<p class="backup-downloads">Download: <a href="/api/transactions" download="transactions.json">Transactions</a> · <a href="/api/savings-ppk" download="savings-ppk.json">Savings &amp; PPK</a></p>
<details id="restorePanel"><summary id="restoreToggle">Restore Backup</summary>
<p class="hint">Replace selected data from JSON. Download a current backup first.</p>
<div class="restore-fields"><div><label for="restoreKind">Backup Type</label><select class="inp" id="restoreKind"><option value="transactions">Transactions</option><option value="savings-ppk">Savings &amp; PPK</option></select></div>
<div><label for="restoreFile">JSON File · Max 64 KiB</label><input class="inp" id="restoreFile" type="file" accept=".json,application/json"></div></div>
<button type="button" class="addbtn" id="restorePreview" disabled>Preview Backup</button>
<div id="restoreDetails" hidden><p class="hint" id="restoreSummary"></p><p class="hint" id="restoreNames"></p><label class="restore-confirm" for="restoreConfirm"><input type="checkbox" id="restoreConfirm"><span id="restoreConfirmLabel">I understand this replaces the selected saved data.</span></label><button type="button" class="rmtext" id="restoreApply" disabled>Restore Backup</button></div>
<p class="hint" id="restoreStatus" role="status" aria-live="polite"></p></details></section>
)BACKUP";

static const char RESTORE_SCRIPT[] PROGMEM = R"BACKUP(
<script>
(function(){
'use strict';
var preview=null,busy=false,uncertain=false,pending=null;
var kind=el('restoreKind'),file=el('restoreFile'),status=el('restoreStatus'),button=el('restoreApply');
function refreshControls(){
 var locked=busy||uncertain||config.storageWritable===false;
 kind.disabled=locked;file.disabled=locked;el('restorePreview').disabled=locked||!file.files.length;
 el('restoreConfirm').disabled=locked;button.disabled=busy||!preview||(!uncertain&&!el('restoreConfirm').checked);
 el('restoreToggle').setAttribute('aria-disabled',String(busy||uncertain));
 if(busy||uncertain)el('restorePanel').open=true;
 el('settingsClose').disabled=busy;
 document.querySelectorAll('[data-tracker-save]').forEach(function(b){b.disabled=busy||uncertain;});
}
function reset(){if(busy||uncertain)return;preview=null;pending=null;el('restoreDetails').hidden=true;el('restoreConfirm').checked=false;status.textContent='';button.textContent='Restore Backup';refreshControls();}
kind.onchange=reset;file.onchange=reset;el('restoreConfirm').onchange=refreshControls;
el('restoreToggle').onclick=function(event){if(busy||uncertain)event.preventDefault();};
el('settingsDialog').addEventListener('cancel',function(event){if(busy)event.preventDefault();});
el('restorePreview').onclick=async function(){
 if(busy||uncertain||!file.files.length)return;var selected=file.files[0];
 reset();if(!selected.size||selected.size>65536){status.textContent='Choose a nonempty JSON backup no larger than 64 KiB.';return;}
 busy=true;refreshControls();status.textContent='Uploading and validating backup…';
 try{
  var body=new FormData();body.append('backup',selected);
  var result=await requestJson('/api/restore-preview?kind='+encodeURIComponent(kind.value)+'&base='+encodeURIComponent(config.revision||0),{method:'POST',body:body}),data=result.data;
  if(!result.response.ok||!data.ok||!data.token)throw new Error(data.error||'Could not validate backup.');
  preview=data;el('restoreDetails').hidden=false;
  var transactions=data.kind==='transactions';
  el('restoreSummary').textContent=transactions?'Replace '+data.replacedCount+' saved transactions with '+data.count+' transactions. The current ticker list and alerts are kept; missing tickers are added.':'Replace '+data.replacedCount+' saved accounts with '+data.count+' accounts and '+data.valuations+' valuations. Ticker transactions are untouched.';
  el('restoreNames').textContent=(data.names||[]).length?(transactions?'Tickers after restore: ':'Accounts: ')+data.names.join(', '):'The backup is empty; restoring it clears the selected data.';
  el('restoreConfirmLabel').textContent='I understand this replaces all saved '+(transactions?'transactions.':'Savings & PPK accounts and history.');
  status.textContent='Backup validated. Review the replacement above, then confirm. Preview expires after 10 minutes.';
 }catch(error){status.textContent=error.message+' No saved data was changed.';}
 finally{busy=false;refreshControls();}
};
button.onclick=async function(){
 if(busy||!preview||(!uncertain&&!el('restoreConfirm').checked))return;
 if(!pending)pending=new URLSearchParams({format:'json',restoreKind:preview.kind,restoreToken:preview.token,base:preview.revision,rid:transactionRequestId(),confirm:'replace'});
 busy=true;refreshControls();status.textContent=uncertain?'Checking the previous restore request…':'Restoring backup…';var responseReceived=false;
 try{
  var result=await requestJson('/api/restore',{method:'POST',body:pending});responseReceived=true;
  if(!result.response.ok||!result.data.ok)throw new Error(result.data.error||'Could not restore backup.');
  status.textContent='Backup restored. Reloading…';location.reload();
 }catch(error){
  if(!responseReceived){uncertain=true;button.textContent='Retry Restore';status.textContent='The response was lost; the restore may already be saved. Retry this unchanged request, or refresh and check your data before uploading another file.';}
  else{uncertain=false;pending=null;preview=null;el('restoreDetails').hidden=true;status.textContent=error.message+' No new restore was applied. Preview the backup again.';}
 }finally{busy=false;refreshControls();}
};
if(config.storageWritable===false)status.textContent='Storage is unavailable. Download existing backups before repairing storage.';
refreshControls();
})();
</script>
)BACKUP";
