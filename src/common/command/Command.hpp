#pragma once

enum class Command : unsigned char {
    select,
    del,
    exists,
    move,
    rename,
    renamenx,
    type,
    set,
    get,
    getRange,
    mget,
    setnx,
    setRange,
    strlen,
    mset,
    msetnx,
    incr,
    incrBy,
    decr,
    decrBy,
    append,
    hdel,
    hexists,
    hget,
    hgetAll
};
