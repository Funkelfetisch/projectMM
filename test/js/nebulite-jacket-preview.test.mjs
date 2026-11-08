import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const load = async file => import('data:text/javascript;base64,' + Buffer.from(readFileSync(new URL(file, import.meta.url), 'utf8')).toString('base64'));
const {drawJacketPreview} = await load('../../src/ui/nebulite-jacket-preview.js');
const {productProjectionPoints} = await load('../../src/ui/nebulite-setup.js');

test('jacket artwork consumes its 30 ordered outputs', () => {
    const samples=[], images=[], colours=[];
    const ctx={fillStyle:'',globalCompositeOperation:'source-over',fillRect(){},save(){},restore(){},translate(){},scale(){},
        createRadialGradient(){return {addColorStop(_offset, value){colours.push(value);}}},
        drawImage(image){images.push(image);}};
    const canvas={width:300,height:300,dataset:{},getContext:()=>ctx,setAttribute(){}};
    const art={texture:{complete:true,naturalWidth:400},garment:{complete:true,naturalWidth:400}};
    const projection=productProjectionPoints({name:'NEBULITE Jacket S3',state:{Layouts:{},Drivers:{}}});
    const drawn=drawJacketPreview(canvas, 0, x=>{samples.push(Math.floor(x*30));return [20,40,60];},projection,art);
    assert.equal(drawn,true);assert.deepEqual(samples,Array.from({length:30},(_,i)=>i));
    assert.equal(images.length,3);assert.equal(colours.length,60);
    assert.equal(canvas.dataset.previewRenderer,'jacket-30-simulation');
    assert.equal(drawJacketPreview(canvas,0,()=>[],{points:[]},art),false);
    assert.equal(drawJacketPreview(canvas,0,()=>[],{points:[...projection.points,[10,10]]},art),false);
});

test('the 2021 jacket draws its 50 outputs on the same artwork, and other counts fall back', () => {
    const samples=[];
    const ctx={fillStyle:'',globalCompositeOperation:'source-over',fillRect(){},save(){},restore(){},translate(){},scale(){},
        createRadialGradient(){return {addColorStop(){}}},drawImage(){}};
    const canvas={width:300,height:300,dataset:{},getContext:()=>ctx,setAttribute(){}};
    const art={texture:{complete:true,naturalWidth:400},garment:{complete:true,naturalWidth:400}};
    const points=Array.from({length:50},(_,i)=>[i%20,i%24]);
    assert.equal(drawJacketPreview(canvas,0,x=>{samples.push(Math.floor(x*50));return [1,2,3];},{points},art),true);
    assert.deepEqual(samples,Array.from({length:50},(_,i)=>i));
    assert.equal(drawJacketPreview(canvas,0,()=>[],{points:points.slice(1)},art),false);
});
