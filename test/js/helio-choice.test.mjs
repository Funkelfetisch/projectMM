import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../../src/ui/helio-orchestration.js', import.meta.url), 'utf8');
const { helioChoiceIndex, planHelioEffectActivation } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

test('HELIO resolves named fixture and palette readbacks alongside numeric controls', () => {
    assert.equal(helioChoiceIndex({value:'RGBW', options:['RGB','GRB','RGBW']}), 2);
    assert.equal(helioChoiceIndex({value:'Warm', options:[{name:'Cold'},{name:'Warm'}]}), 1);
    assert.equal(helioChoiceIndex({value:1, options:['linear','gamma']}), 1);
    assert.equal(helioChoiceIndex({value:'deleted', options:['RGB']}), -1);
});

test('HELIO switches a scene only after disabling competing effects and applying controls', () => {
    const effects = [{name:'Old', enabled:true}, {name:'Twinkles', enabled:false, controls:[{name:'speed',value:20}]}];
    assert.deepEqual(planHelioEffectActivation(effects, 'Twinkles', {speed:135}), [
        {module:'Old',control:'enabled',value:false},
        {module:'Twinkles',control:'speed',value:135},
        {module:'Twinkles',control:'enabled',value:true},
    ]);
});
