// Copyright 2010 Vanilla Hsu<vanilla@FreeBSD.org>

#include <libmemcached/memcached.h>
#include <node.h>
#include <node_events.h>

#define DEBUGMODE 1
#define pdebug(...) do{if(DEBUGMODE)printf(__VA_ARGS__);}while(0)
#define THROW_BAD_ARGS \
  v8::ThrowException(v8::Exception::TypeError(v8::String::New("Bad arguments")))

typedef enum {
  _ADD,
  _REPLACE,
  _PREPEND,
  _APPEND
} _cmd;

enum _type {
  MEMC_GET,
  MEMC_SET,
  MEMC_INCR,
  MEMC_DECR,
  MEMC_ADD,
  MEMC_REPLACE,
  MEMC_APPEND,
  MEMC_PREPEND,
  MEMC_CAS,
  MEMC_REMOVE,
  MEMC_FLUSH
};

typedef enum {
  MVAL_STRING = 1,
  MVAL_LONG,
  MVAL_BOOL
} mval_type;

typedef struct {
  mval_type type;
  union {
    char *c;
    uint64_t l;
    int b;
  } u;
} mval;

using namespace v8;
using namespace node;

static Persistent<String> ready_symbol;
static Persistent<String> result_symbol;
static Persistent<String> close_symbol;
static Persistent<String> connect_symbol;
static Persistent<String> distribution_symbol;

class Connection : EventEmitter {
 public:
  static void
  Initialize(Handle<Object> target)
  {
    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName(String::NewSymbol("Connection"));

    ready_symbol = NODE_PSYMBOL("ready");
    result_symbol = NODE_PSYMBOL("result");
    close_symbol = NODE_PSYMBOL("close");
    connect_symbol = NODE_PSYMBOL("connect");
    distribution_symbol = NODE_PSYMBOL("distribution");

    NODE_SET_PROTOTYPE_METHOD(t, "addServer", AddServer);
    NODE_SET_PROTOTYPE_METHOD(t, "_get", _Get);
    NODE_SET_PROTOTYPE_METHOD(t, "_set", _Set);
    NODE_SET_PROTOTYPE_METHOD(t, "_incr", _Incr);
    NODE_SET_PROTOTYPE_METHOD(t, "_decr", _Decr);
    NODE_SET_PROTOTYPE_METHOD(t, "_add", _Add);
    NODE_SET_PROTOTYPE_METHOD(t, "_replace", _Replace);
    NODE_SET_PROTOTYPE_METHOD(t, "_prepend", _Prepend);
    NODE_SET_PROTOTYPE_METHOD(t, "_append", _Append);
    NODE_SET_PROTOTYPE_METHOD(t, "_cas", _Cas);
    NODE_SET_PROTOTYPE_METHOD(t, "_remove", _Remove);
    NODE_SET_PROTOTYPE_METHOD(t, "_flush", _Flush);
    NODE_SET_PROTOTYPE_METHOD(t, "close", Close);

    t->PrototypeTemplate()->SetAccessor(distribution_symbol,
        DistributionGetter, DistributionSetter);

    target->Set(String::NewSymbol("Connection"), t->GetFunction());
  }

  bool AddServer(const char *hostname, int port)
  {
    rc = memcached_server_add(&memc_, hostname, port);
    if (rc != MEMCACHED_SUCCESS)
      return false;

    memcached_behavior_set(&memc_, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);
    rc = memcached_version(&memc_);
    if (rc != MEMCACHED_SUCCESS)
      return false;

    return true;
  }

  uint64_t _GetDistribution(void)
  {
    uint64_t data;

    data = memcached_behavior_get(&memc_, MEMCACHED_BEHAVIOR_DISTRIBUTION);

    return data;
  }

  void _SetDistribution(uint64_t data)
  {
    memcached_behavior_set(&memc_, MEMCACHED_BEHAVIOR_DISTRIBUTION, data);
  }

  void _Get(const char *key, int key_len)
  {
    size_t value;
    uint32_t flags;

    memcached_attach_fd(key, key_len);
    mval_.u.c = memcached_get(&memc_, key, key_len, &value, &flags, &rc);
    if (mval_.u.c != NULL) {
      mval_.type = MVAL_STRING;
    }
  }

  void _Set(const char *key, int key_len, const char *value, int value_len,
      time_t expiration)
  {
    memcached_attach_fd(key, key_len);
    rc = memcached_set(&memc_, key, key_len, value, value_len, expiration, 0);
    if (rc == MEMCACHED_SUCCESS) {
      mval_.type = MVAL_BOOL;
      mval_.u.b = 1;
    }
  }

  void _Incr(const char *key, int key_len, uint32_t offset)
  {
    uint64_t value;

    memcached_attach_fd(key, key_len);
    rc = memcached_increment(&memc_, key, key_len, offset, &value);
    if (rc == MEMCACHED_SUCCESS) {
      mval_.type = MVAL_LONG;
      mval_.u.l = value;
    }
  }

  void _Decr(const char *key, int key_len, uint32_t offset)
  {
    uint64_t value;

    memcached_attach_fd(key, key_len);
    rc = memcached_decrement(&memc_, key, key_len, offset, &value);
    if (rc == MEMCACHED_SUCCESS) {
      mval_.type = MVAL_LONG;
      mval_.u.l = value;
    }
  }

  void _Cmd(_cmd cmd, const char *key, int key_len,
      const char *value, int value_len)
  {
    switch (cmd) {
      case _ADD:
        rc = memcached_add(&memc_, key, key_len, value, value_len, 0, 0);
        break;
      case _REPLACE:
        rc = memcached_replace(&memc_, key, key_len, value, value_len, 0 ,0);
        break;
      case _PREPEND:
        rc = memcached_prepend(&memc_, key, key_len, value, value_len, 0 ,0);
        break;
      case _APPEND:
        rc = memcached_append(&memc_, key, key_len, value, value_len, 0 ,0);
        break;
      default:
        rc = (memcached_return_t) -1;
    }

    if (rc == MEMCACHED_SUCCESS) {
      mval_.type = MVAL_BOOL;
      mval_.u.b = 1;
    }
  }

  void _Cas(const char *key, int key_len, const char *value, int value_len,
      uint64_t cas_arg)
  {
    memcached_attach_fd(key, key_len);
    rc = memcached_cas(&memc_, key, key_len, value, value_len, 0, 0, cas_arg);
    if (rc == MEMCACHED_SUCCESS) {
      mval_.type = MVAL_BOOL;
      mval_.u.b = 1;
    }
  }

  void _Remove(const char *key, size_t key_len, time_t expiration = 0)
  {
    memcached_attach_fd(key, key_len);
    rc = memcached_delete(&memc_, key, key_len, expiration);
    if (rc == MEMCACHED_SUCCESS) {
      mval_.type = MVAL_BOOL;
      mval_.u.b = 1;
    }
  }

  void _Flush(time_t expiration)
  {
    memcached_attach_fd(NULL, 0);
    rc = memcached_flush(&memc_, expiration);
    if (rc == MEMCACHED_SUCCESS) {
      mval_.type = MVAL_BOOL;
      mval_.u.b = 1;
    }
  }

  void Close(Local<Value> exception = Local<Value>())
  {
    ev_io_stop(EV_DEFAULT_ &read_watcher_);
    ev_io_stop(EV_DEFAULT_ &write_watcher_);

    if (exception.IsEmpty()) {
      Emit(close_symbol, 0, NULL);
    } else {
      Emit(close_symbol, 1, &exception);
    }
  }

  const char *ErrorMessage()
  {
    return memcached_strerror(NULL, rc);
  }

 protected:
  static Handle<Value> DistributionGetter(Local<String> property, const AccessorInfo& info)
  {
    Connection *c = ObjectWrap::Unwrap<Connection>(info.This());
    assert(c);
    assert(property == distribution_symbol);

    HandleScope scope;
    Local<Integer> v = Integer::New(c->_GetDistribution());
    return scope.Close(v);
  }

  static void DistributionSetter(Local<String> property, Local<Value> value, const AccessorInfo& info)
  {
    Connection *c = ObjectWrap::Unwrap<Connection>(info.This());
    assert(c);

    c->_SetDistribution(value->IntegerValue());
  }

  static Handle<Value> New(const Arguments &args)
  {
    Connection *c = new Connection();
    c->Wrap(args.This());

    return args.This();
  }

  static Handle<Value> AddServer(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsInt32()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value server_name(args[0]->ToString());
    int32_t port = args[1]->Int32Value();
    bool r = c->AddServer(*server_name, port);

    if (!r) {
      return ThrowException(Exception::Error(
            String::New(c->ErrorMessage())));
    }

    c->Emit(connect_symbol, 0, NULL);
    return Undefined();
  }

  static Handle<Value> _Get(const Arguments &args)
  {
    if (args.Length() < 1 || !args[0]->IsString()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());

    c->_Get(*key, key.length());

    return Undefined();
  }

  static Handle<Value> _Set(const Arguments &args)
  {
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsInt32()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    String::Utf8Value value(args[1]->ToString());
    uint32_t expiration = args[2]->Int32Value();

    c->_Set(*key, key.length(), *value, value.length(), expiration);

    return Undefined();
  }

  static Handle<Value> _Incr(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsInt32()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    uint32_t offset = args[1]->Int32Value();

    c->_Incr(*key, key.length(), offset);

    return Undefined();
  }

  static Handle<Value> _Decr(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsInt32()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    uint32_t offset = args[1]->Int32Value();

    c->_Decr(*key, key.length(), offset);

    return Undefined();
  }

  static Handle<Value> _Add(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    String::Utf8Value value(args[1]->ToString());

    c->_Cmd(_ADD, *key, key.length(), *value, value.length());

    return Undefined();
  }

  static Handle<Value> _Replace(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    String::Utf8Value value(args[1]->ToString());

    c->_Cmd(_REPLACE, *key, key.length(), *value, value.length());

    return Undefined();
  }

  static Handle<Value> _Prepend(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    String::Utf8Value value(args[1]->ToString());

    c->_Cmd(_PREPEND, *key, key.length(), *value, value.length());

    return Undefined();
  }

  static Handle<Value> _Append(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    String::Utf8Value value(args[1]->ToString());

    c->_Cmd(_APPEND, *key, key.length(), *value, value.length());

    return Undefined();
  }

  static Handle<Value> _Cas(const Arguments &args)
  {
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() ||
        args[2]->IsNumber()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    String::Utf8Value value(args[1]->ToString());
    uint64_t cas_arg = args[2]->IntegerValue();

    c->_Cas(*key, key.length(), *value, value.length(), cas_arg);

    return Undefined();
  }

  static Handle<Value> _Remove(const Arguments &args)
  {
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsInt32()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    String::Utf8Value key(args[0]->ToString());
    time_t expiration = args[1]->Int32Value();

    c->_Remove(*key, key.length(), expiration);

    return Undefined();
  }

  static Handle<Value> _Flush(const Arguments &args)
  {
    if (args.Length() < 1 || !args[0]->IsInt32()) {
      return THROW_BAD_ARGS;
    }

    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    time_t expiration = args[0]->Int32Value();

    c->_Flush(expiration);

    return Undefined();
  }

  static Handle<Value> Close(const Arguments &args)
  {
    Connection *c = ObjectWrap::Unwrap<Connection>(args.This());
    c->Close();
    return Undefined();
  }

  void Event(int revents)
  {
    Handle<Value> args;

    if (revents & EV_ERROR) {
      Close();
      return;
    }

    if (revents & EV_READ) {
      return;
    }

    if (revents & EV_WRITE) {
      switch (mval_.type) {
        case MVAL_STRING:
          args = String::New(mval_.u.c);
          free(mval_.u.c);
          mval_.type = (mval_type)0;
          rc = (memcached_return_t)-1;
          break;
        case MVAL_LONG:
          args = Integer::New(mval_.u.l);
          mval_.u.l = 0;
          mval_.type = (mval_type)0;
          rc = (memcached_return_t)-1;
          break;
        case MVAL_BOOL:
          args = Boolean::New(mval_.u.b);
          mval_.u.b = 0;
          mval_.type = (mval_type)0;
          rc = (memcached_return_t)-1;
          break;
        default:
          args = Exception::Error(String::New(memcached_strerror(NULL, rc)));
      }

      ev_io_stop(EV_DEFAULT_ &write_watcher_);
      Emit(result_symbol, 1, &args);
      Emit(ready_symbol, 0, NULL);
    }
  }

  Connection() : node::EventEmitter() {
    memcached_create(&memc_);
    rc = (memcached_return_t)-1;
    mval_.type = (mval_type)0;
    
    ev_init(&read_watcher_, io_event);
    read_watcher_.data = this;
    ev_init(&write_watcher_, io_event);
    write_watcher_.data = this;
  }

 private:
  static void io_event(EV_P_ ev_io *w, int revents)
  {
    Connection *c = static_cast<Connection *>(w->data);
    c->Event(revents);
  }

  void memcached_attach_fd(const char *key, int key_len)
  {
    int server, fd;

    if (key == NULL || key_len == 0)
      server = memc_.number_of_hosts - 1;
    else
      server = memcached_generate_hash(&memc_, key, key_len);

    if (memc_.servers[server].fd == -1)
      memcached_version(&memc_);

    fd = memc_.servers[server].fd;

    ev_io_set(&read_watcher_, fd, EV_READ);
    ev_io_set(&write_watcher_, fd, EV_WRITE);
    ev_io_start(EV_DEFAULT_ &write_watcher_);
  }

  memcached_st memc_;
  ev_io read_watcher_;
  ev_io write_watcher_;
  mval mval_;
  memcached_return_t rc;
};

extern "C" void
init(Handle<Object> target)
{
  NODE_DEFINE_CONSTANT(target, MEMC_GET);
  NODE_DEFINE_CONSTANT(target, MEMC_SET);
  NODE_DEFINE_CONSTANT(target, MEMC_INCR);
  NODE_DEFINE_CONSTANT(target, MEMC_DECR);
  NODE_DEFINE_CONSTANT(target, MEMC_ADD);
  NODE_DEFINE_CONSTANT(target, MEMC_REPLACE);
  NODE_DEFINE_CONSTANT(target, MEMC_APPEND);
  NODE_DEFINE_CONSTANT(target, MEMC_PREPEND);
  NODE_DEFINE_CONSTANT(target, MEMC_CAS);
  NODE_DEFINE_CONSTANT(target, MEMC_REMOVE);
  NODE_DEFINE_CONSTANT(target, MEMC_FLUSH);

  NODE_DEFINE_CONSTANT(target, MEMCACHED_DISTRIBUTION_MODULA);
  NODE_DEFINE_CONSTANT(target, MEMCACHED_DISTRIBUTION_CONSISTENT);
  NODE_DEFINE_CONSTANT(target, MEMCACHED_DISTRIBUTION_CONSISTENT_KETAMA);
  NODE_DEFINE_CONSTANT(target, MEMCACHED_DISTRIBUTION_RANDOM);
  NODE_DEFINE_CONSTANT(target, MEMCACHED_DISTRIBUTION_CONSISTENT_KETAMA_SPY);
  NODE_DEFINE_CONSTANT(target, MEMCACHED_DISTRIBUTION_CONSISTENT_MAX);

  Connection::Initialize(target);
}
