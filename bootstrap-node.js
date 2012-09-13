// @@@LICENSE
//
//      Copyright (c) 2009-2012 Hewlett-Packard Development Company, L.P.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// LICENSE@@@
/*global global,require,MojoLoader:true,IMPORTS:true,console:true,process */
MojoLoader = global['mojoloader'] ? global['mojoloader'] : require('mojoloader');
var palmbus = global['palmbus'] ? global['palmbus'] : require('palmbus');
var fs = global['fs'] ? global['fs'] : require('fs');
var webos = global['webos'] ? global['webos'] : require('webos');

var debugMsgs=false;
var locale=undefined;
var region=undefined;
var phoneRegion=undefined;
var offset=undefined;
var timezone=undefined;
var TZ=undefined;
IMPORTS = {"require":require};
appController = undefined;

// Patch to convert legacy http calls to new ones
if (process.version >= "v0.4.0") {
	(function () {
		var http = require('http');
		var https = require('https');
		var EventEmitter = require('events').EventEmitter;
		http.createClient = function (port, host, secure) {
			var module = secure ? https : http;
			var client = new EventEmitter();
			var options = {
				port: port,
				host: host
			};
			client.request = function (method, path, headers) {
				options.method = method;
				options.path = path;
				options.headers = headers
				var request = module.request(options, function (response) {});
				return request;
			};
			return client;
		};
	
	}());
}

function loadFile(path) {
	try	{
		return fs.readFileSync(path, "utf8");
	} catch (e) {
		
	}
	return "";
}

function writeGroupFile(groupfile) {
	try	{
		return fs.writeFileSync(groupfile, process.pid.toFixed(0) + "\n", "utf8");
	} catch (e) {
		console.error("Failure writing to tasks file " + JSON.stringify(groupfile) + " : " + JSON.stringify(e));
	}
	return "";
}

function loadSource()
{
	try
	{
		var files = JSON.parse(loadFile("sources.json", "utf8"));
		var len = files.length;
		var i = 0;
		for (; i < len; i++)
		{
			if (!files[i].override)
			{
				break;
			}
			MojoLoader.override(files[i].override);
		}

		var mojolibname = MojoLoader.builtinLibName("mojoservice", "1.0");
		IMPORTS.mojoservice = global['mojolibname'] ? global['mojolibname'] : MojoLoader.require({ name: "mojoservice", version: "1.0" }).mojoservice;

		for (; i < len; i++)
		{
			var file = files[i];
			file.source && webos.include(file.source);
		
			if (file.library) {
				var libname = MojoLoader.builtinLibName(file.library.name, file.library.version);
				if (!global[libname]) {
					IMPORTS[file.library.name] = MojoLoader.require(file.library)[file.library.name];
				}
				else
				{
					IMPORTS[file.library.name] = global[libname];
				}
			}	
		}
	}
	catch (e)
	{
		if (file)
		{
			console.error("Loading failed in:", file.source || file.library.name);
		}
		console.error(e.stack || e);
	}
}

function loadAndStart() {
	loadSource();
	appController = new IMPORTS.mojoservice.AppController(paramsToScript);
}

console = global['pmloglib'] ? global['pmloglib'] : require('pmloglib');
// read config
var paramsIndex;
var params = process.argv;
var paramsCount = params.length;
var paramsToScript = [];
for (paramsIndex = 2; paramsIndex < paramsCount; ++ paramsIndex) {
    if (params[paramsIndex] === "--") {
        paramsIndex += 1;
        break;
    }
}

var remainingCount = paramsCount - paramsIndex;
if (remainingCount > 0) {
    paramsToScript = params.slice(paramsIndex, paramsIndex+remainingCount);
   
    try { 
        var cgroup = paramsToScript.splice(0, 1);
        writeGroupFile(cgroup[0]);
    } catch (e) {
        console.error("Unable to get cgroup: " + e);
    }
}

try {
	var dir = paramsToScript[0];
	var config = JSON.parse(loadFile("services.json", "utf8"));
	var name = config["services"][0].name;
	var max_len = 63;
	var cname=name;
	var namel=name.length;
	if (namel > max_len) {
		var i=0;
		while (i < namel && i != -1 && (namel - i) > max_len) {
			i = cname.indexOf(".", i+1);
		}
		if (i > -1) {
			cname=cname.substring(i+1);
		} else {
			cname=cname.substring(name.length-max_len);
		}
	}
	console.name = cname;
} catch (e) {
	console.error("parsing services.json failed with:" + e);
}

var shortname = name.slice(name.length-12)+".js";
var args = paramsToScript.slice(0);
args[0]=name+".js";
process.setArgs(args);
process.setName({"shortname":shortname});

//console.error("cgroup: " + cgroup);
//console.error("Args are: " + JSON.stringify(args));
//console.error("pTS is: " +JSON.stringify(paramsToScript));

if (process.getuid() === 0) {
	try {
		var publicRolePath  = dir+"/roles/pub/"+name+".json";
		var privateRolePath = dir+"/roles/prv/"+name+".json";
		//console.info("registering public");		
		var publicHandle = new palmbus.Handle(null, true);
		//console.info("pushing public role "+publicRolePath);		
		publicHandle.pushRole(publicRolePath);
		//console.info("registering private");
		var privateHandle = new palmbus.Handle(null, false);
		//console.info("pushing private role "+privateRolePath);		
		privateHandle.pushRole(privateRolePath);
	}
	catch (e) {
		console.error("pushRole failed with:" + e);
		return;
	}
}

loadAndStart();
