// Existing credentials stay on-device; the input holds only a user's replacement.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { src, fnSource } from './app-source.mjs';

test('password controls do not decode a stored credential from public state', () => {
    assert.ok(!src.includes('decodePassword'));
    const control = fnSource('createControl');
    const at = control.indexOf('case "password": {');
    const password = control.slice(at, control.indexOf('case "select":', at));
    assert.ok(password.includes('input.value = ""'));
    assert.ok(password.includes('Saved - enter to replace'));
    assert.ok(!password.includes('input.value = ctrl.value'));
});
test('list credentials use the same set-only control and state pushes retain a draft', () => {
    assert.ok(fnSource('fillEditableListDetail').includes('createControl(rowMid, null, ctrl, write)'));
    const update = fnSource('updateModuleControls');
    const at = update.indexOf('case "password": {');
    const password = update.slice(at, update.indexOf('case "select":', at));
    assert.ok(password.includes('input.placeholder'));
    assert.ok(!password.includes('input.value ='));
});
test('a submitted replacement is retained in public UI state only as a configured flag', () => {
    assert.ok(fnSource('sendControl').includes('ctrl.type === "password" ? Boolean(value) : value'));
});
