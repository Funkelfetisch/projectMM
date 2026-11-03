import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const url = source => `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
const codec = readFileSync(new URL('../../src/ui/nebulite-recording.js', import.meta.url), 'utf8');
const source = readFileSync(new URL('../../src/ui/nebulite-legacy-import.js', import.meta.url), 'utf8')
    .replace("'./nebulite-recording.js'", JSON.stringify(url(codec)));
const { prepareLegacyImport, commitLegacyImport } = await import(url(source));
const names = ['red','green','blue','white','brightness','segment start','segment stop'];
function fixture() {
    const catalog = '85872f1e2e02', namesToSave = [];
    const template = { $origin:{scene:'nebulite-solid',catalog},
        $uiSchema:{effectType:'NebuliteSolidEffect',effectPath:['Effects','L','S'],
            controls:names.map(name=>({name,min:0,max:65535}))},
        Effects:{L:{S:{type:'NebuliteSolidEffect', ...Object.fromEntries(names.map(name=>[name,0]))}}} };
    const candidate = {status:'review',name:'Blue',scene:'nebulite-solid',source_preset_sha256:'a'.repeat(64),
        source_recording_sha256:'039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81',
        source_segments:[{fx:0,pal:1,start:0,stop:1,col:[[1,2,3,0]]}],controls:{red:1,green:2,blue:3,white:0,brightness:255,'segment start':0,'segment stop':1}};
    const bundle = {format:'nebulite-wled-review-v1',product:'croptop',catalog,
        items:[{candidate,recording:'AQID'},{candidate:{status:'unsupported',name:'Missing'}}]};
    const client={listTemplates:async()=>({catalog}),list:async()=>({limit:64,items:[]}),
        readTemplate:async()=>({document:structuredClone(template)}),
        write:async(name,doc,options)=>{namesToSave.push({name,doc,options});return {stored:true};}};
    return {bundle, client, namesToSave};
}

function audioFixture() {
    const result=fixture(), catalog=result.bundle.catalog;
    const controls={speed:128,'effect intensity':128,'audio floor':0,'simulate if no source':true,'frame ms':23,'segment start':0,'segment stop':1};
    const template={$origin:{scene:'nebulite-noisefire',catalog},
        $uiSchema:{effectType:'NebuliteNoisefireEffect',effectPath:['Effects','L','S'],
            controls:Object.keys(controls).map(name=>name==='simulate if no source'?{name,type:'bool'}:{name,min:0,max:65535})},
        Effects:{L:{S:{type:'NebuliteNoisefireEffect',...controls}}}};
    result.client.readTemplate=async()=>({document:structuredClone(template)});
    result.bundle.items=[{candidate:{status:'review',name:'♪ Noisefire',scene:'nebulite-noisefire',
        source_preset_sha256:'a'.repeat(64),source_is_matrix:false,source_recording_sha256:'039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81',
        source_segments:[{fx:143,pal:1,sx:128,ix:128,si:0,m12:2,start:0,stop:1,col:[[1,2,3,0]]}],controls},recording:'AQID'}];
    return result;
}

test('historical music names are made portable and source geometry is explicit', async () => {
    const {bundle,client}=audioFixture();
    const review=await prepareLegacyImport(JSON.stringify(bundle),'croptop',client);
    assert.equal(review.ready.length,1);
    assert.equal(review.ready[0].sourceName,'♪ Noisefire');
    assert.equal(review.ready[0].name,'Music - Noisefire');
    assert.match(review.ready[0].warnings.join(' '),/1D-to-2D arc mapping/);
    assert.match(review.ready[0].warnings.join(' '),/audio levels are not calibrated/i);
    bundle.items[0].candidate.source_segments[0].m12=3;
    assert.equal((await prepareLegacyImport(JSON.stringify(bundle),'croptop',client)).ready.length,0);
    bundle.items[0].candidate.source_segments[0].m12=2;
    bundle.items[0].candidate.source_is_matrix=true;
    assert.equal((await prepareLegacyImport(JSON.stringify(bundle),'croptop',client)).ready.length,0);
});

test('review checks source and does not create anything until explicit commit', async () => {
    const {bundle,client,namesToSave}=fixture();
    const review=await prepareLegacyImport(JSON.stringify(bundle),'croptop',client);
    assert.equal(review.ready.length,1); assert.equal(review.skipped.length,1); assert.equal(namesToSave.length,0);
    const progress=[]; const result=await commitLegacyImport(review,client,(...args)=>progress.push(args));
    assert.deepEqual(result.saved,['Blue']); assert.equal(result.failed,null);
    assert.equal(namesToSave[0].options.create,true); assert.equal(progress.at(-1)[0],1);
});

test('catalogue and preset index reads are serial on the controller', async () => {
    const {bundle,client}=fixture();
    let release;
    const gate=new Promise(resolve=>{release=resolve;});
    let listed=false;
    client.listTemplates=async()=>{await gate;return {catalog:bundle.catalog};};
    client.list=async()=>{listed=true;return {limit:64,items:[]};};
    const pending=prepareLegacyImport(JSON.stringify(bundle),'croptop',client);
    await Promise.resolve();
    assert.equal(listed,false);
    release();
    assert.equal((await pending).ready.length,1);
    assert.equal(listed,true);
});

test('wrong product, wrong catalogue, duplicates and lost writes remain safe', async () => {
    const {bundle,client,namesToSave}=fixture();
    await assert.rejects(prepareLegacyImport(JSON.stringify(bundle),'jacket',client),/match this product/);
    await assert.rejects(prepareLegacyImport(JSON.stringify({...bundle,catalog:'000000000000'}),'croptop',client),/catalogue/);
    client.list=async()=>({limit:64,items:[{name:'Blue'}]});
    const duplicate=await prepareLegacyImport(JSON.stringify(bundle),'croptop',client);
    assert.equal(duplicate.ready[0].name,'Blue (WLED)');
    client.list=async()=>({limit:64,items:[{name:'Blue'},{name:'Blue (WLED)'}]});
    const repeated=await prepareLegacyImport(JSON.stringify(bundle),'croptop',client);
    assert.equal(repeated.ready.length,0); assert.equal(repeated.skipped[0].reason,'A preset with this name already exists');
    client.list=async()=>({limit:64,items:[]});
    const review=await prepareLegacyImport(JSON.stringify(bundle),'croptop',client);
    client.write=async(name)=>{namesToSave.push(name);throw new Error('network result unknown');};
    const result=await commitLegacyImport(review,client);
    assert.equal(result.failed,'Blue'); assert.deepEqual(result.saved,[]); assert.deepEqual(namesToSave,['Blue']);
});

test('the import form requires review and explicit commit before saving', async () => {
    const {bundle,client,namesToSave}=fixture();
    const element = tag => ({ tag, children: [], listeners: {}, textContent: '',
        setAttribute() {}, append(...items) { this.children.push(...items); },
        replaceChildren(...items) { this.children = items; },
        addEventListener(type, fn) { this.listeners[type] = fn; } });
    const cardsSource = readFileSync(new URL('../../src/ui/helio-preset-cards.js', import.meta.url), 'utf8')
        .replace(/^import[^\n]+\n/gm, '').replace('export class HelioPresetCards', 'class HelioPresetCards');
    const Cards=vm.runInNewContext(cardsSource+'\nHelioPresetCards;',
        { document:{createElement:element}, prepareLegacyImport, commitLegacyImport });
    const cards=new Cards({client,legacyProduct:()=> 'croptop'});
    cards.host={}; cards.mount=async()=>{cards.status={textContent:''};};
    const details=cards.legacyImportForm(), form=details.children[1];
    const input=form.children[1].children[0], review=form.children[2], commit=form.children[3], status=form.children[4];
    assert.equal(commit.disabled,true);
    const text=JSON.stringify(bundle); input.files=[{size:text.length,text:async()=>text}];
    await form.listeners.submit({preventDefault(){}});
    assert.equal(namesToSave.length,0); assert.equal(review.disabled,false); assert.equal(commit.disabled,false);
    assert.match(status.textContent,/1 ready/);
    await commit.listeners.click();
    assert.equal(namesToSave.length,1); assert.match(cards.status.textContent,/1 historical looks imported/);
});

test('the import review visibly discloses the source geometry and missing audio fallback', async () => {
    const {bundle,client}=audioFixture();
    const element = tag => ({tag,children:[],listeners:{},textContent:'',setAttribute(){},
        append(...items){this.children.push(...items);},replaceChildren(...items){this.children=items;},
        addEventListener(type,fn){this.listeners[type]=fn;}});
    const cardsSource=readFileSync(new URL('../../src/ui/helio-preset-cards.js',import.meta.url),'utf8')
        .replace(/^import[^\n]+\n/gm,'').replace('export class HelioPresetCards','class HelioPresetCards');
    const Cards=vm.runInNewContext(cardsSource+'\nHelioPresetCards;',
        {document:{createElement:element},prepareLegacyImport,commitLegacyImport});
    const cards=new Cards({client,legacyProduct:()=> 'croptop'}), form=cards.legacyImportForm().children[1];
    const text=JSON.stringify(bundle);form.children[1].children[0].files=[{size:text.length,text:async()=>text}];
    await form.listeners.submit({preventDefault(){}});
    assert.match(form.children[5].children[0].textContent,/♪ Noisefire → Music - Noisefire/);
    assert.match(form.children[5].children[0].textContent,/1D-to-2D arc mapping/);
    assert.match(form.children[5].children[0].textContent,/historical beat is simulated/);
});

test('new music preset suggestions are valid for the ASCII preset store', async () => {
    const element=tag=>({tag,children:[],listeners:{},textContent:'',value:'',open:false,
        get options(){return this.children;},get selectedOptions(){return this.children.filter(c=>c.value===this.value).slice(0,1);},
        append(...items){this.children.push(...items);},appendChild(item){this.children.push(item);if(this.tag==='select'&&!this.value)this.value=item.value;},
        addEventListener(type,fn){this.listeners[type]=fn;},setAttribute(){}});
    const cardsSource=readFileSync(new URL('../../src/ui/helio-preset-cards.js',import.meta.url),'utf8')
        .replace(/^import[^\n]+\n/gm,'').replace('export class HelioPresetCards','class HelioPresetCards');
    const Cards=vm.runInNewContext(cardsSource+'\nHelioPresetCards;', {document:{createElement:element}});
    const client={listTemplates:async()=>({items:[{id:'noisefire',name:'♪ Noisefire'},{id:'rainbow',name:'♫ Rainbow'}]})};
    const form=new Cards({client}).addForm(), inner=form.children[1], select=inner.children[0].children[0], name=inner.children[1].children[0];
    form.open=true;await form.listeners.toggle();
    assert.equal(name.value,'Music - Noisefire copy');
    assert.ok(new RegExp(`^(?:${name.pattern})$`).test(name.value));
    select.value='rainbow';select.listeners.change();
    assert.equal(name.value,'Music - Rainbow copy');
    assert.ok(!new RegExp(`^(?:${name.pattern})$`).test('Unsafe/Name'));
    assert.ok(!new RegExp(`^(?:${name.pattern})$`).test('Unsafe.Name'));
});
