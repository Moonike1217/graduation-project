let s = "{\"a\":1,\"b\":\"hello\"}";
let obj = JSON.parse(s);
let a = obj.a;
let b = obj.b;
let out = JSON.stringify(obj);
print(a);
print(b);
print(out);
