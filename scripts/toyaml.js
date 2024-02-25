// simple script to dump JS object as YAML
// based on https://github.com/jeffsu/json2yaml

function dumpYaml(obj, options)
{
  const flowLevel = options.flowLevel || 100;  // switch to flow style beyond this indentation level
  const alwaysFlow = options.alwaysFlow || [];  // always use flow style for specified key names
  const extraLines = options.extraLines || 0;  // add (extraLines - level) lines between map/hash blocks
  const indent = options.indent || "  ";
  const quote = options.quote || '"';

  function spacing(level) { return level < flowLevel ? indent.repeat(level) : ""; }

  function normalizeString(str) {
    if (str.match(/^[\w\$][\w\$\-\.]*$/))
      return str;
    return quote + str + quote;
    //return '"'+escape(str).replace(/%u/g,'\\u').replace(/%U/g,'\\U').replace(/%/g,'\\x')+'"';
  }

  function getType(obj) {
    const type = typeof obj;
    if (obj instanceof Array) return 'array';
    if (type == 'string') return 'string';
    if (type == 'boolean') return 'boolean';
    if (type == 'number') return 'number';
    if (type == 'undefined' || obj === null) return 'null';
    return 'hash';
  }

  function convert(obj, level) {
    const type = getType(obj);
    switch(type) {
      case 'array':
        return convertArray(obj, level);
      case 'hash':
        return convertHash(obj, level);
      case 'string':
        return normalizeString(obj);
      case 'null':
        return 'null';
      case 'number':
        return obj.toString();
      case 'boolean':
        return obj ? 'true' : 'false';
    }
  }

  function convertArray(obj, level) {
    if(obj.length === 0)
      return '[]';
    if(level >= flowLevel)
      return "[" + obj.map(x => convert(x, flowLevel)).join(", ") + "]";
    return obj.map(x => spacing(level) + "- " + convert(x, flowLevel)).join("\n");
  }

  function convertHash(obj, level) {
    var ret = [];
    for (var k in obj) {
      if (obj.hasOwnProperty(k)) {
        var type = getType(obj[k]);
        if(type == 'array' && obj[k].length == 0) type = 'empty';
        if(type == 'hash' && Object.keys(obj[k]).length == 0) type = 'empty';
        if ((type != 'array' && type != 'hash') || level+1 >= flowLevel || alwaysFlow.includes(k)) {
          ret.push(spacing(level) + normalizeString(k) + ': ' + convert(obj[k], flowLevel));
        } else {
          ret.push(spacing(level) + normalizeString(k) + ':\n' + convert(obj[k], level+1));
        }
      }
    }
    if(ret.length == 0)
      return "{}";
    if(level >= flowLevel)
      return "{ " + ret.join(", ") + " }";
    return ret.join("\n".repeat(Math.max(1, 1+extraLines - level)));
  }

  if (typeof obj == 'string')
    obj = JSON.parse(obj);
  return convert(obj, 0);
}

module.exports.dump = dumpYaml;
