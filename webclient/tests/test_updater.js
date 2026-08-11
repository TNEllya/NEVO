'use strict';
const assert = require('assert');
const U = require('../electron/updater.js');

let pass = 0, fail = 0;
function t(cond, msg) { if (cond) { pass++; } else { fail++; console.error('  FAIL:', msg); } }

// 版本解析
t(JSON.stringify(U.parseVersion('BETA0.0.1')) === '[0,0,1]', 'parseVersion BETA0.0.1');
t(JSON.stringify(U.parseVersion('v1.2.3-beta')) === '[1,2,3]', 'parseVersion v1.2.3-beta');
t(JSON.stringify(U.parseVersion('0.0.0')) === '[0,0,0]', 'parseVersion 0.0.0');
t(JSON.stringify(U.parseVersion('garbage')) === '[0,0,0]', 'parseVersion garbage');

// 版本比较
t(U.isNewerVersion('BETA0.0.2', 'BETA0.0.1') === true, '0.0.2 > 0.0.1');
t(U.isNewerVersion('BETA0.0.1', 'BETA0.0.1') === false, 'equal is not newer');
t(U.isNewerVersion('BETA0.0.1', 'BETA0.1.0') === false, '0.0.1 < 0.1.0');
t(U.isNewerVersion('1.10.0', '1.9.9') === true, 'patch compare');

console.log(`\nResult: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
