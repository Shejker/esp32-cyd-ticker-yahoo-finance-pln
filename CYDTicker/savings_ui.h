#pragma once
#include <Arduino.h>

// The caller escapes the account name before inserting it into HTML.
String savingsRow(int id, bool ppk, const String& escapedName, const String& date,
                  double value, double gain) {
  String row = "<tr class='manual-row'><td><a class='account-name' href='#account-" + String(id)
    + "' data-account-id='" + String(id) + "' title='Manage account'>" + escapedName
    + "</a><span class='account-kind'>" + (ppk ? "PPK" : "Savings")
    + " · " + date + "</span></td><td class='tnowrap'>" + String(value, 2)
    + "</td><td class='tnowrap'>" + String(value - gain, 2)
    + "</td><td class='tnowrap " + (gain >= 0 ? String("pl-pos") : String("pl-neg"))
    + "'>" + (gain >= 0 ? String("+") : String()) + String(gain, 2)
    + "</td></tr>";
  return row;
}

static const char SAVINGS_DIALOG[] PROGMEM = R"HTML(
<dialog id="manualDialog" aria-labelledby="manualTitle">
<div class="settings-heading"><h3 id="manualTitle"></h3><button type="button" class="settings-close" id="manualClose" aria-label="Close">×</button></div>
<p id="manualBase" class="hint"></p>
<form id="manualActionForm">
<div id="manualActionPicker" hidden><label for="manualAction">Action</label><select class="inp" id="manualAction"></select></div>
<p id="manualDeleteNotice" class="hint" hidden>This removes the account and its saved history. Download a <a href="/api/savings-ppk" download="savings-ppk.json">JSON backup</a> first.</p>
<div id="manualIdentity" hidden>
<label for="manualKind">Account Type</label><select class="inp" id="manualKind" name="kind"><option value="savings">Savings</option><option value="ppk">PPK</option></select>
<div id="manualNameGroup"><label for="manualName">Name</label><input class="inp" id="manualName" name="name" maxlength="60" required></div>
</div>
<div id="manualOpening" hidden>
<p class="hint">New account: leave both amounts at zero. Existing account: enter its balance and total previous net contributions, including employer and government contributions for PPK.</p>
<label for="manualOpeningValue">Opening Balance PLN</label><input class="inp" id="manualOpeningValue" name="openingValue" type="number" min="0" step="0.01" required value="0">
<label for="manualCapital">Previous Net Contributions PLN</label><input class="inp" id="manualCapital" name="capital" type="number" min="0" step="0.01" required value="0">
</div>
<div id="manualSavings" hidden>
<label for="manualDeposit">Deposit PLN</label><input class="inp" id="manualDeposit" name="deposit" type="number" step="0.01" required value="0">
<p class="hint">Enter only this operation. Use a negative amount for a withdrawal.</p>
<label for="manualInterest">New Interest Credited PLN</label><input class="inp" id="manualInterest" name="interest" type="number" min="0" step="0.01" required value="0">
<p class="hint">Enter only newly credited net interest, not the cumulative total.</p>
</div>
<div id="manualPPK" hidden>
<label for="manualEmployee">Your Contribution PLN</label><input class="inp" id="manualEmployee" name="employee" type="number" min="0" step="0.01" required value="0">
<label for="manualEmployer">Employer Contribution PLN</label><input class="inp" id="manualEmployer" name="employer" type="number" min="0" step="0.01" required value="0">
<label for="manualState">Government Contribution PLN</label><input class="inp" id="manualState" name="state" type="number" min="0" step="0.01" required value="0">
<p class="hint">Enter only new contributions. Employer and government contributions increase capital, not investment profit.</p>
</div>
<div id="manualValuation" hidden>
<label for="manualValue">Current Account Value PLN</label><input class="inp" id="manualValue" name="value" type="number" min="0" step="0.01">
<p id="manualValuationHint" class="hint"></p>
</div>
<div id="manualDateGroup"><label for="manualDate">Operation / Valuation Date</label><input class="inp" id="manualDate" name="date" type="date" required><p class="hint">Record operations chronologically. Same-day updates create one daily chart point.</p></div>
<p id="manualPreview" class="meta" aria-live="polite"></p>
<p id="manualStatus" class="hint" role="status" aria-live="polite"></p>
<button type="submit" id="manualSubmit">Save</button>
</form></dialog>
)HTML";

static const char SAVINGS_SCRIPT[] PROGMEM = R"SCRIPT(
<script>
(function(){
'use strict';
var cfg=JSON.parse(document.getElementById('tracker-config').textContent),accounts=cfg.manual||[];
var el=function(id){return document.getElementById(id);},current=null,operation='',busy=false,savingsName='';
var money=new Intl.NumberFormat('pl-PL',{minimumFractionDigits:2,maximumFractionDigits:2});
function dateToday(){var parts={};new Intl.DateTimeFormat('en-CA',{timeZone:'Europe/Warsaw',year:'numeric',month:'2-digit',day:'2-digit'}).formatToParts(new Date()).forEach(function(p){parts[p.type]=p.value;});return parts.year+'-'+parts.month+'-'+parts.day;}
function showGroup(id,show){var group=el(id);group.hidden=!show;group.querySelectorAll('input,select').forEach(function(input){input.disabled=!show;});}
function syncName(){
 var automatic=operation==='create'&&el('manualKind').value==='ppk',input=el('manualName');
 if(automatic){if(!input.disabled)savingsName=input.value;input.value='PPK';}
 else if(operation==='create'&&input.disabled)input.value=savingsName;
 showGroup('manualNameGroup',(operation==='create'||operation==='edit')&&!automatic);
}
function number(id){var input=el(id);return input.value===''?0:input.valueAsNumber;}
function preview(){
 var value=current?current.value:0,gain=current?current.gain:0,capital=value-gain;
 if(operation==='valuation'&&current&&current.kind==='ppk'&&value===0&&gain===0){el('manualPreview').textContent='Record PPK contributions first using Contributions. Then update the valuation to calculate fund performance.';return;}
 if(operation==='create'){value=number('manualOpeningValue');capital=number('manualCapital');gain=value-capital;}
 else if(operation==='savings'){value+=number('manualDeposit')+number('manualInterest');gain+=number('manualInterest');capital=value-gain;}
 else if(operation==='ppk-deposit'){var deposit=number('manualEmployee')+number('manualEmployer')+number('manualState');capital+=deposit;value=el('manualValue').value!==''?number('manualValue'):value+deposit;gain=value-capital;}
 else if(operation==='valuation'){if(el('manualValue').value===''){el('manualPreview').textContent='Enter the account value to calculate profit automatically.';return;}value=number('manualValue');gain=value-capital;}
 el('manualPreview').textContent=[value,gain,capital].every(Number.isFinite)?'After saving: balance '+money.format(value)+' PLN · net contributions '+money.format(capital)+' PLN · profit/loss '+money.format(gain)+' PLN':'Check the amounts.';
}
function open(op,id,alreadyOpen){
 if(busy)return;operation=op;current=accounts.find(function(a){return a.id===id;})||null;
 el('manualActionForm').reset();el('manualStatus').textContent='';el('manualDate').value=dateToday();el('manualDate').max=dateToday();
 var title={create:'Add Savings / PPK',edit:'Account Name and Type',savings:'Deposit / Interest','ppk-deposit':'New PPK Contributions',valuation:current&&current.kind==='ppk'?'Update Valuation':'Update Balance',delete:'Remove Account'};
 var actions=el('manualAction');actions.replaceChildren();
 if(current){[current.kind==='ppk'?'ppk-deposit':'savings','valuation','edit','delete'].forEach(function(action){actions.add(new Option(title[action],action));});actions.value=op;}
 showGroup('manualActionPicker',!!current);el('manualDeleteNotice').hidden=op!=='delete';el('manualSubmit').textContent=op==='delete'?'Remove account':'Save';
 el('manualTitle').textContent=title[op]+(current?' — '+current.name:'');
 el('manualBase').textContent=current?'Current balance: '+money.format(current.value)+' PLN · net contributions: '+money.format(current.value-current.gain)+' PLN · profit/loss: '+money.format(current.gain)+' PLN':'';
 showGroup('manualIdentity',op==='create'||op==='edit');showGroup('manualOpening',op==='create');showGroup('manualSavings',op==='savings');showGroup('manualPPK',op==='ppk-deposit');showGroup('manualValuation',op==='ppk-deposit'||op==='valuation');showGroup('manualDateGroup',op!=='edit'&&op!=='delete');
 el('manualValue').required=op==='valuation';el('manualValue').value='';
 el('manualValuationHint').textContent=op==='ppk-deposit'?'Optional: current balance from your PPK provider. Otherwise, the balance is the last valuation plus new contributions, with profit unchanged. Update the valuation to include fund performance.':'Enter the current account balance. Recorded net contributions are deducted to calculate profit or loss. Do not enter contributions again.';
 savingsName='';el('manualName').value=current?current.name:'';el('manualKind').value=current&&current.kind==='ppk'?'ppk':'savings';syncName();
 el('manualPreview').hidden=op==='edit'||op==='delete';preview();if(!alreadyOpen)el('manualDialog').showModal();
}
document.getElementById('manualRows').addEventListener('click',function(event){var link=event.target.closest('[data-account-id]');if(!link)return;event.preventDefault();var id=Number(link.dataset.accountId),account=accounts.find(function(a){return a.id===id;});
 if(account)open(account.kind==='ppk'?'ppk-deposit':'savings',id);
});
el('manualAction').onchange=function(){if(current)open(this.value,current.id,true);};
el('manualKind').onchange=syncName;
el('manualAdd').onclick=function(event){event.preventDefault();open('create',-1);};el('manualAdd').hidden=accounts.length>=4;
el('manualClose').onclick=function(){if(!busy)el('manualDialog').close();};
el('manualDialog').addEventListener('cancel',function(event){if(busy)event.preventDefault();});
el('manualActionForm').addEventListener('input',preview);
async function send(body,button){
 if(busy)return;busy=true;button.disabled=true;el('manualClose').disabled=true;
 var inputs=Array.from(el('manualActionForm').querySelectorAll('input:not(:disabled),select:not(:disabled)'));inputs.forEach(function(input){input.disabled=true;});
 el('manualStatus').textContent='Saving…';el('manualGlobalStatus').textContent='';
 try{var result=await requestJson('/manual-action',{method:'POST',body:body}),response=result.response,data=result.data;if(!response.ok||!data.ok)throw new Error(data.error||'Could not save.');location.reload();}
 catch(error){el('manualStatus').textContent=error.message+' If the device response was lost, refresh and check the balance before retrying.';el('manualGlobalStatus').textContent=el('manualStatus').textContent;}
 finally{busy=false;button.disabled=false;el('manualClose').disabled=false;inputs.forEach(function(input){input.disabled=false;});}
}
el('manualActionForm').addEventListener('submit',function(event){
 event.preventDefault();if(busy)return;var form=new FormData(this),body=new URLSearchParams();form.forEach(function(value,key){body.set(key,value);});body.set('op',operation);body.set('format','json');
 if(current){body.set('id',current.id);body.set('version',current.version);}
 if(operation==='delete'&&!confirm('Remove '+current.name+' and its history?'))return;
 if(operation==='create'){body.set('value',body.get('openingValue'));if(el('manualKind').value==='ppk')body.set('name','PPK');}
 if(operation==='create'&&el('manualKind').value==='ppk'&&number('manualOpeningValue')>0&&number('manualCapital')===0){el('manualStatus').textContent='For an existing PPK account, enter total previous contributions too.';return;}
 if(operation==='valuation'&&current.kind==='ppk'&&current.value===0&&current.gain===0&&number('manualValue')>0){el('manualStatus').textContent='Record PPK contributions first so they are not counted as profit.';return;}
 if(operation==='savings'&&number('manualDeposit')===0&&number('manualInterest')===0){el('manualStatus').textContent='Enter a deposit or new interest.';return;}
 if(operation==='ppk-deposit'&&number('manualEmployee')+number('manualEmployer')+number('manualState')===0){el('manualStatus').textContent='Enter new contributions. To update only the account value, use Update valuation.';return;}
 send(body,el('manualSubmit'));
});
})();
</script>
)SCRIPT";
