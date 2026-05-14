var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var customClay = require('./customclay.js');
var clay = new Clay(clayConfig, customClay);
