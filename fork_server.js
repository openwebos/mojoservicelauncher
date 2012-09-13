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

var cwd = process.cwd();

global.sandbox = {};
global.sandbox.prettyArgs = process.env.NODE_PRETTY_ARGS;
process.env.NODE_PRETTY_ARGS = null;
process.setName({shortname: "node_fork_server"});

global.sandbox.preforkMem = process.memoryUsage();

global.sandbox.log = function (msg) {
	if (global['pmloglib']) {
		global['pmloglib'].log("[node_fork_server] [" + process.uptime() + "] " + msg);
	} else {
		console.log("[node_fork_server] [" + process.uptime() + "] " + msg);
	}
};

var preload = ["webos", "pmloglib", "mojoloader", "palmbus"];
for (var i = 0, ni = preload.length; i < ni; i++) {
	try {
		var module = preload[i];
		global[module] = require(module);
	} catch (e) {
		global.sandbox.log("Failed to pre-load " + preload[i]);
	}
}

var MojoLoader = global.mojoloader;
var mojo_preload = [
                        { name: "foundations", version: "1.0"},
                        { name: "mojoservice", version: "1.0" },
                        { name: "foundations.json", version: "1.0" },
                        { name: "foundations.xml", version: "1.0" },
                        { name: "globalization", version: "1.0" },
                        { name: "underscore", version: "1.0" },
                   ];

for (i=0, ni = mojo_preload.length; i < ni; i++) {
    var libname = MojoLoader.builtinLibName(mojo_preload[i].name, mojo_preload[i].version);
    global[libname] = MojoLoader.require(mojo_preload[i])[mojo_preload[i].name];
}

var fs = require('fs');
var child_process = require('child_process');
var net = require('net');
var stderr = require('sys').debug;
var stdout = require('sys').log;
var Buffer = require('buffer').Buffer;
var path = require('path');
var Module = require('module');

global.sandbox.parseMessage = function(stream, chunk, msgHandler) {
	if (stream.buffered) {
		var expanded = new Buffer(stream.buffered.length + chunk.length);
		stream.buffered.copy(expanded, 0, 0, stream.buffered.length);
		chunk.copy(expanded, stream.buffered.length, 0, chunk.length);
		stream.buffered = expanded;
	} else {
		stream.buffered = chunk;
	}

	//global.sandbox.log("Parsing messages ('" + stream.buffered + "', " + stream.buffered.length + ")");
	var messageStart = 0;
	for (var i = 0, ni = stream.buffered.length; i < ni; i++) {
		if (stream.buffered[i] == 0) {
			var message = stream.buffered.slice(messageStart, i)
			//global.sandbox.log("Found message '" + message + "'");
			try {
				msgHandler(stream, message);
			} catch (e) {
				global.sandbox.log("Exception in message handler for '" + message + "': " + e);
			}
			messageStart = i + 1;
		}
	}

	if (messageStart != 0) {
		if (messageStart < stream.buffered.length) {
			stream.buffered = stream.buffered.slice(messageStart, stream.buffered.length);
		} else {
			stream.buffered = null;
		}
	} else {
		//global.sandbox.log("No message found in '" + stream.buffered + "'");
	}
};

global.sandbox.sendMessage = function(stream, obj) {
	var objStr = JSON.stringify(obj);
	var msg = new Buffer(Buffer.byteLength(objStr, 'utf8') + 1);
	msg.write(objStr);
	msg[msg.length - 1] = 0;
	return stream.write(msg);
};

String.prototype.startsWith = function(prefix) {
	if (this.length < prefix.length) {
		return false;
	}
	if (this.length == prefix.length) {
		return this === prefix;
	}
	for (var i = 0, ni = prefix.length; i < ni; i++) {
		if (this[i] != prefix[i]) {
			return false;
		}
	}
	return true;
};


global.sandbox.forkHandler = function(request, spawnRequestStream) {
	// need a more persistent sandbox for some of the
	// convenience functions
	var __spawnSandbox = {
		log : global.sandbox.log,
		sendMessage : global.sandbox.sendMessage
	};

	//__spawnSandbox.log("Fork setup code...");

	global.sandbox.spawnServer.removeAllListeners();
	global.sandbox.spawnServer.close();
	spawnRequestStream.removeAllListeners();
	spawnRequestStream.destroy();
	spawnRequestStream = null;
	process.env.NODE_PRETTY_ARGS = global.sandbox.prettyArgs;
	request.args.unshift(process.argv[0], request.script);

	//console.log("delete global.sandbox returns: " + delete global.sandbox);

	__spawnSandbox.log(process.pid + " successfully forked");
	{
		var isURL;
		process.argv = process.ARGV = request.args;

		// logic from node.js.  argv[0] doesn't need to be adjusted
		// since it's already adjusted when the fork daemon starts up
		if (process.argv[1].charAt(0) != "/" && !(isURL = process.argv[1].startsWith("http://"))) {
			process.argv[1] = path.join(cwd, process.argv[1]);
		}

		__spawnSandbox.originalScript = request.script;

		//require(process.argv[1]);
		//sandbox.endsWith = function(str, prefix) {
		//	if (str.length < prefix.length)
		//		return false;
		//	if (str.length == prefix.length)
		//		return str == prefix;
		//	for (var i = str.length - 1, j = prefix.length - 1; j >= 0; j--, i--) {
		//		if (str[i] != prefix[j--])
		//			return false;
		//	}
		//	return true;
		//}
		//if (isURL === undefined) {isURL = sandbox.startsWith(process.argv[1], "http://")}
		//if (!isURL && !sandbox.endsWith(process.argv[1], ".js")) {
		//	process.argv[1] = process.argv[1] + ".js";
		//}

		// NOTE: this will block the script from exiting even if it can
		// the fork server has to close stdin (which happens automatically
		// when the remote side goes away).
		var stdin = process.openStdin();
		stdin.on('error', function(e) {
			// EAGAIN is a weird error, but we don't care about it
		});
	}
	//__spawnSandbox.log("Running user script");
	// clear references to variables that aren't necessary in the child
	request = undefined;
        Module.runMain();
	if (parseInt(process.env.NODE_PRETTY_ARGS)) {
		process.argv.splice(0, 1);
		process.argv[0] = __spawnSandbox.originalScript;
		process.setArgs(process.argv);

		var friendlyName = process.basename(__spawnSandbox.originalScript);
		process.setName({name: process.argv[0], shortname: process.basename(__spawnSandbox.originalScript)});
	}

	//__spawnSandbox.log("Notifying child is ready");

	{
		var notifyReady = process.forked_script_notifier = net.createConnection(9000);
		notifyReady.on('connect', function() {
			//__spawnSandbox.log(process.argv[1] + " startup finished - notifying fork daemon");
			__spawnSandbox.sendMessage(this, {pid : process.pid, child_ready: true});
			this.end();
			this.removeAllListeners();
		});
		notifyReady.on('error', function(e) {
			__spawnSandbox.log("Trouble notifying fork daemon of startup - dying:\n" + e);
			process.exit(-1);
		});
		notifyReady.on('end', function() {
			this.destroy();
		});
	}
};

global.sandbox.streamMsgHandler = function(_stream, data) {
	//global.sandbox.log("incoming spawn request: '" + data + "'");
	var request;
	try {
		request = JSON.parse(data.toString());
	} catch(e) {
		global.sandbox.log("Problem parsing '" + data + "' (" + data.length + ") : " + e);
		return;
	}
	if (request.spawn) {
		if (!global.children) {
			global.children = {};
		}
		if (!request.args) {
			request.args = [];
		} else {
			// first arg is directory that the child should be
			// running in
			var dir = request.args.shift();
			if (dir) {
				global.sandbox.log("Changing to directory: " + dir);
				process.chdir(dir);
			} else {
				global.sandbox.log("Unable to get directory from args");
			}
                }

		var forkedChild;

		if (global.sandbox.compacted === undefined) {
			global.sandbox.compacted = true;
			if (process.gc) {
				process.gc();
				process.gc();
				process.gc();
			} else {
				global.sandbox.log("GC not exposed");
			}
		}

		try {
			forkedChild = child_process.fork(global.sandbox.forkHandler, request, _stream);
		} catch (e) {
			global.sandbox.log("Trouble spawning script: " + e);
			global.sandbox.sendMessage(_stream, {spawned: false, errMsg : JSON.stringify(e)});
			_stream.end();
			return;
		}

		global.sandbox.log("(" + forkedChild.pid + ") " + "Forked script " + request.script + " childstdout: " + request.childstdout + " childstderr: " + request.childstderr);

		forkedChild.terminateIO = function() {
			this.streamToParent = null;
			this.stdin.end();
			this.stdout.end();
			this.stderr.end();
			this.stdin.destroy();
			this.stdout.destroy();
			this.stderr.destroy();
			this.stdin = null;
			this.stdout = null;
			this.stderr = null;
		};

		var forkedPid = forkedChild.pid;
		forkedChild.on('exit', function(code, signum) {
			if (this.streamToParent) {
				global.sandbox.log("Notified remote connection that spawned child finished");
				global.sandbox.sendMessage(this.streamToParent, {dead: true, exitCode : code, signal : signum});
				this.streamToParent.end();
				this.streamToParent = null;
			}
			global.sandbox.log("(" + forkedPid + ") " + "Exited with " + (code == null ? "no exit code" : code) + (signum == null ? "" : ", signal " + signum));
			// make sure to clear dangling reference so that memory cleanup
			// can happen (in case the child didn't spawn successfully)
			eval('global.children.child' + this.pid + ' = null;');

			if (this.pendingKill)
				clearTimeout(this.pendingKill);
		});

		// the requestor may have been sending us data on stdin
		// that we should forward to the spawned child
		if (_stream.bufferedInput) {
			forkedChild.stdin.write(_stream.bufferedInput);
			_stream.bufferedInput = null;
		}
		forkedChild.stdout.server = forkedChild;
		forkedChild.stderr.server = forkedChild;

		forkedChild.stdout.on('data', function(data) {
			if (forkedChild.streamToParent) {
				global.sandbox.sendMessage(forkedChild.streamToParent, {stderr: data.toString()});
			} else {
				global.sandbox.log("Pipe back to spawner is dead - closing i/o");
				this.end();
				forkedChild.stderr.end();
				forkedChild.stdin.end();
			}
		});
		forkedChild.stderr.on('data', function(data) {
			if (forkedChild.streamToParent) {
				global.sandbox.sendMessage(forkedChild.streamToParent, {stdout : data.toString()});
			}
		});
		forkedChild.stdin.on('error', function(e) {
			// FIXME: EBADF always occurs on stdin when the child
			// exits.  The stack trace shows this comes from
			// self._readImpl() failure in initStream in lib/net.js
			if (e.message != "EBADF, Bad file descriptor") {
				global.sandbox.log("Error on stdin pipe from spawned child: " + e);
			}
		});
		forkedChild.stdout.on('error', function(e) {
			global.sandbox.log("Error on stdout pipe from spawned child: " + e);
		});

		forkedChild.stderr.on('error', function(e) {
			global.sandbox.log("Error on stderr pipe from spawned child: " + e);
		});

		// NOTE: !!!! creating a reference loop which will leak memory
		//            if it's not disposed of properly
		forkedChild.streamToParent = _stream;
		_stream.child = forkedChild;

		// NOTE: reference to spawned child process is made global.  will leak memory
		//       if not disposed of properly. the only purpose is so that
		eval('global.children.child' + forkedChild.pid + " = forkedChild;");
		return;
	}

	if (request.child_ready) {
		var child;
		var requestPid = parseInt(request.pid);
		if (!requestPid) {
			global.sandbox.log("Invalid pid from spawned child: " + request.pid);
			return;
		}

		eval('child = global.children.child' + requestPid + ';');

		// we know we can destroy this reference now since it has served its purpose
		// (e.g. we can have the connection the child opens proxy to the connection back to
		// the original process)
		eval('global.children.child' + requestPid + ' = null;');
		if (!child) {
			global.sandbox.log("!!!!!!!!!! Child for " + requestPid + " is already marked as dead");
			return;
		}

		global.sandbox.log("Sending notification that child with pid: " + requestPid + " is ready");

		var notifyReady = {spawned : true, pid : request.pid};
		var response = JSON.stringify(notifyReady);
		//global.sandbox.log("Child " + notifyReady.pid + " has started " + child.streamToParent.remoteAddress + ": '" + response + "'");
		global.sandbox.sendMessage(child.streamToParent, notifyReady);
		return;
	}

	if ("string" === typeof request.stdin) {
		if (!_stream.child) {
			_stream.bufferedInput += "" + request.stdin;
		} else {
			_stream.child.stdin.write(request.stdin);
		}
		return;
	}

	if (typeof request.terminate == "object") {
		_stream.terminateChild(request.terminate.signal, request.terminate.gracePeriod);
	}

	global.sandbox.log("Unhandled request '" + data + "' from " + _stream.remoteAddress);
};

// amount of ms to way after a SIGTERM to generate a sigkill
// -1 to indicate never
global.sandbox.KillDelay = 2000;
global.sandbox.spawnServerHandler = function(stream) {
	stream.setNoDelay();
	stream.on('connect', function() {
		//global.sandbox.log("Connection from " + this.remoteAddress);
	});
	stream.terminateChild = function(level, delay) {
		if (delay === undefined || delay === null)
			delay = global.sandbox.KillDelay;

		if (this.child) {
			this.terminateChildIO();
			try {
				this.child.kill(level);
			} catch (e) {
				global.sandbox.log("Invalid kill level specified: " + level);
				this.child.kill();
			}
			if (delay >= 0) {
				this.child.pendingKill = setTimeout(function() {
					if (stream.child) {
						global.sandbox.log("Child " + stream.child.pid + " still alive after " + delay + " ms - forcing the issue");
						stream.child.kill('SIGKILL');
					}
				}, delay);
			}
			return true;
		}
		return false;
	}

	// returns true if there was a child attached to this stream
	// false otherwise
	stream.terminateChildIO = function() {
		if (this.child) {
			this.child.terminateIO();
			return true;
		}
		return false;
	}

	stream.on('end', function() {
		if (this.terminateChildIO()) {
			eval('global.children.child' + stream.child.pid + ' = null;');
			global.sandbox.log("(" + stream.child.pid + ") " + "Console connection ended (remote = " + this.remoteAddress + ")");
			this.child = null;
		}
		this.end();
		this.destroy();
	});
	stream.on('error', function(e) {
		global.sandbox.log("Connection error: " + e);
		if (this.terminateChildIO()) {
			eval('global.children.child' + stream.child.pid + ' = null;');
			global.sandbox.log("(" + stream.child.pid + ") " + "Console connection was broken (remote = " + this.remoteAddress + ")");
			this.child = null;
		}

		this.end();
		this.destroy();
	});
	stream.on('data', function(data_) {
		global.sandbox.parseMessage(this, data_, global.sandbox.streamMsgHandler);
	});
};

global.sandbox.spawnServerListening = function() {
	var upstart_job = process.env['UPSTART_JOB'];
	var emit_ready = false;
	var stat = undefined;

	// Only emit the upstart "ready" event once per boot to avoid
	// re-triggering dependent jobs in the event of a crash or restart
	try {
		stat = fs.statSync('/tmp/node_fork_server.upstart');
		global.sandbox.log('Upstart file exists');
	} catch (e) {}

	if (!stat) {
		global.sandbox.log('Upstart file does not exist; creating');
		try {
			fs.writeFileSync('/tmp/node_fork_server.upstart', '');
		} catch (e) {
			global.sandbox.log('Warning: unable to create upstart flag: ' + e.message);
		}
		emit_ready = true;
	}


	if (emit_ready && upstart_job) {
		global.sandbox.log('Emitting upstart event');
		child_process.exec("/sbin/initctl emit --no-wait " + upstart_job + "-ready", 
			function (error, stdout, stderr) {
				if (error !== null) {
					global.sandbox.log('upstart emit exec error: ' + error);
				}
		});
	}
};

global.sandbox.spawnServer = net.createServer(global.sandbox.spawnServerHandler);
global.sandbox.spawnServer.listen(9000, global.sandbox.spawnServerListening);
global.sandbox.log("Ready");
