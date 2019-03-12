(function() {
 'use strict';

 var st = new ShardingTest({
shards: 3,
other: {
c0: {},  // Make sure 1st config server is primary
c1: {rsConfig: {priority: 0}},
c2: {rsConfig: {priority: 0}}
}
});

 var primarycs=st.configRS.getPrimary();var configSecondaryList = st.configRS.getSecondaries();var mgs=st.s0;
 var admin=mgs.getDB('admin');
 var numchunk=Math.floor(Math.random()+50);
 jsTest.log("--------numchunk------------------------------"+numchunk);
 assert.commandWorked(admin.runCommand({enableSharding:"testDB"}));
 assert.commandWorked(admin.runCommand({shardCollection:"testDB.foo",key:{a:1}}));
 assert.commandWorked(admin.runCommand({shardCollection:"testDB.foo1",key:{a:"hashed"},numInitialChunks:numchunk}))
 var cfg=mgs.getDB('config');
 var coll=mgs.getCollection("testDB.foo");
var testdb=mgs.getDB('testDB');
// coll.drop();
assert.writeOK(coll.insert({a: 1, Value: 'Test value 1'}));
assert.writeOK(coll.insert({a: 2, Value: 'Test value 10'}));
assert.writeOK(coll.insert({a: 3, Value: 'Test value 20'}));
assert.writeOK(coll.insert({a: 4, Value: 'Test value 30'}));
printShardingStatus(st.config,false);
//	st.stopBalancer();
//	var balance = cfg.settings.find().toArray();
//	assert.eq(true,balance[0].stopped);
assert.commandWorked(st.moveChunk('testDB.foo', {a: 20}, "shard0000"))
var chunks = cfg.chunks.find({ns:"testDB.foo"}).toArray();
assert.eq("shard0000",chunks[0].shard);
assert.eq(4, coll.find().itcount());
for(var i=1;i<101;i++){
	assert.commandWorked(st.moveChunk('testDB.foo', {a: 20}, "shard0001"));
	assert.commandWorked(st.moveChunk('testDB.foo', {a: 20}, "shard0000"));
	assert.writeOK(coll.insert({a: 5, Value: 'Test value 1'}));
}
sleep(2*1000);
var chunks = cfg.chunks.find({ns:"testDB.foo"}).toArray();
assert.eq("shard0000",chunks[0].shard);
coll.update({"a":11},{"$set":{"value":20}},{upsert:true});
assert.eq(105, coll.find().itcount());
var chunks = cfg.chunks.find().toArray();
assert.eq(numchunk+1,chunks.length);
st.stop();
})();


