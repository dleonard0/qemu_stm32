/* QMP over websocket */

class QMP {

  constructor(uri) {
    this.eventListeners = {}
    this.executorsById = {}
    this.pendingMessages = []
    this.open = false

    this.msgbuf = "";
    this.nextId = 1;

    this.serverQMP = {}
    this.capabilities = undefined

    var socket = new WebSocket(uri);
    socket.addEventListener('open', this);
    socket.addEventListener('message', this);
    socket.addEventListener('error', this);
    socket.addEventListener('close', this);
    this.socket = socket

    var this_ = this
    this.execute('qmp_capabilities').then(function(val) { 
      this_.capabilities = val;
    })
  }

  /*
   * Returns a promise to execute the given command.
   * Consult qmp-commands.txt for the available commands.
   * The resolve functions will be called with the 'return' value,
   * and the reject function will be called with an error constructed
   * from the 'error' object.
   */
  execute(execute_, arguments_) {
    var this_ = this
    var id = "id" + this.nextId++
    var msg = {
      execute: execute_,
      arguments: arguments_,
      id: id
    }
    if (arguments.length < 2)
      delete msg.arguments;
    msg = JSON.stringify(msg)

    return new Promise(function (resolve, reject) {
      var executor = this_.executorsById[id] = {
        resolve: resolve,
        reject: reject
      }
      if (this_.open)
        this_.socket.send(msg)
      else
        this_.pendingMessages.push(msg)
    })
  }

  /* Closes the connection, rejects all pending promises */
  close(code, reason) {
    if (this.open) {
      this.socket.close.apply(this.socket, arguments)
    }
  }

  /* EventTarget interface to receive QMP events */
  addEventListener(type, listener) {
    var typeListeners;
    if (type in this.eventListeners)
      typeListeners = this.eventListeners[type];
    else
      typeListeners = this.eventListeners[type] = [];
    typeListeners.push(listener)
  }

  removeEventListener(type, listener) {
    if (type in this.eventListeners) {
      var typeListeners = this.eventListeners[type];
      var index = typeListeners.indexOf(listener)
      if (index >= 0) delete typeListeners[index];
    }
  }

  dispatchEvent(e) {
    var handled = false;
    if (e.type in this.eventListeners) {
      var typeListeners = this.eventListeners[e.type];
      for (var i in typeListeners) {
        var listener = typeListeners[i];
        if (listener === undefined) {
          /* removed */
        } else if (typeof listener == 'function') {
          listener(e);
          handled = true;
        } else if ('handleEvent' in listener) {
          listener.handleEvent(e);
          handled = true;
        }
      }
    }
    if (!handled) {
      console.log("unhandled QMP event", e)
    }
  }


  /* Private methods follow */

  _handleResponseMessage(msgobj) {
    var executor = this.executorsById[msgobj.id]
    if (executor === undefined) {
      console.log("unexpected response ID", msgobj.id)
      return;
    }
    if ('return' in msgobj) {
      executor.resolve(msgobj['return'])
    } else {
      var name = msgobj.error['class']
      exc = new Error(name + ": " + msgobj.error.desc);
      exc.name = name
      executor.reject(exc)
    }
  }

  handleEvent(event) {
    switch (event.type) {
    case 'open':
      this.open = true;
      this._sendPendingCommands();
      break;
    case 'message':
      /* Buffer message data until we have a well-formed JSON object */
      this.msgbuf += event.data;
      try {
        var msgobj = JSON.parse(this.msgbuf);
        this.msgbuf = "";
        this._handleSocketMessage(msgobj);
      } catch (ex) {
        if (!(ex instanceof SyntaxError))
          throw(e);
      }
      break;
    case 'error':
      this._cancelAllExecutors(new Error('Error: ' + event.data))
      this.close()
      break;
    case 'close':
      this.open = false
      this._cancelAllExecutors(new Error('Closed'))
      break;
    }
  }

  /* Send commands for execute() promises made before the 'open' event */
  _sendPendingCommands() {
    var msgs = this.pendingMessages
    this.pendingMessages = []
    for (var i in msgs) {
      this.socket.send(msgs[i])
    }
  }

  /* Cancel all pending execute() promises */
  _cancelAllExecutors(error) {
    this.pendingMessages = []
    var executorsById = this.executorsById
    this.executorsById = {}
    for (var id in executorsById) {
      var executor = executorsById[id]
      executor.reject(error)
    }
  }

  _handleSocketMessage(msgobj) {
    if ('QMP' in msgobj)
      this.serverQMP = msgobj.QMP;
    else if ('return' in msgobj || 'error' in msgobj)
      this._handleResponseMessage(msgobj);
    else if ('event' in msgobj) {
      /* Convert to an Event-like object */
      this.dispatchEvent({
        type: msgobj.event,
        event: msgobj.event,  /* for backward-compat */
        timestamp: 0 + msgobj.timestamp.seconds +
                     1e-6 * msgobj.timestamp.microseconds,
        target: this,
        data: msgobj.data
      })
    }
  }
}
