let scenes=null,sceneTable='ui_sprites';

function sceneStatus(text){document.getElementById('scenestatus').textContent=text}

function drawSceneTable(){
    if(!scenes)return;
    const select=document.getElementById('scenetable');
    if(!select.options.length){
        for(const name of Object.keys(scenes.tables))select.add(new Option(name,name));
        select.value=sceneTable;
    }
    sceneTable=select.value;
    const table=scenes.tables[sceneTable];
    const wrap=document.getElementById('scenerows');
    wrap.replaceChildren();
    const el=document.createElement('table');
    const head=document.createElement('tr');
    for(const column of ['Remove',...table.header]){
        const th=document.createElement('th');th.textContent=column;head.append(th);
    }
    el.append(head);
    table.rows.forEach((row,index)=>{
        const tr=document.createElement('tr');
        const removeCell=document.createElement('td');
        const remove=document.createElement('button');
        remove.textContent='×';
        remove.onclick=()=>{table.rows.splice(index,1);drawSceneTable()};
        removeCell.append(remove);tr.append(removeCell);
        for(const column of table.header){
            const td=document.createElement('td');
            const input=document.createElement('input');
            input.value=row[column]??'';
            input.onchange=()=>{row[column]=input.value};
            td.append(input);tr.append(td);
        }
        el.append(tr);
    });
    wrap.append(el);
}

function addSceneRow(){
    const table=scenes.tables[sceneTable];
    const row={};
    for(const column of table.header)row[column]='-';
    table.rows.push(row);
    drawSceneTable();
}

async function loadScenes(){
    const response=await fetch('/api/scenes');
    const out=await response.json();
    if(!response.ok)throw Error(out.error);
    scenes=out;
    drawSceneTable();
    const hd=out.tables.hd_models;
    const list=document.getElementById('hdlist');
    list.replaceChildren();
    for(const row of hd.rows){
        const item=document.createElement('div');
        item.className='toolbar';
        item.innerHTML=`<strong>${row.key}</strong> <small>${row.parent} · ${row.fbx} · ${row.model}</small>`;
        list.append(item);
    }
}

document.getElementById('scenetable').onchange=()=>{sceneTable=document.getElementById('scenetable').value;drawSceneTable()};
document.getElementById('addscenerow').onclick=addSceneRow;
document.getElementById('savescenes').onclick=async()=>{
    try{
        await post('/api/scenes',scenes);
        sceneStatus('Scene tables saved. Restart the game to load the edited menus, CSS, HUD, and HD roster.');
    }catch(e){sceneStatus(e.message)}
};
document.getElementById('importhd').onclick=async()=>{
    const button=document.getElementById('importhd');
    await busy(button,async()=>{
        sceneStatus('Importing HD model…');
        const result=await post('/api/hd/import',{
            path:document.getElementById('hdpath').value,
            key:document.getElementById('hdkey').value,
            parent:document.getElementById('hdparent').value
        });
        sceneStatus(`${result.name} imported as ${result.model}. Save scene tables if you edited other rows, then reopen character select.`);
        await loadScenes();
    });
};
loadScenes().catch(e=>sceneStatus(e.message));
