(function() {
    'use strict';

        var st = new ShardingTest({shards: 3, mongos: 1});
        var primarycs=st.configRS.getPrimary();var configSecondaryList = st.configRS.getSecondaries();var mgs=st.s0;
        var admin=mgs.getDB('admin');
        var cfg=mgs.getDB('config');
        var coll=mgs.getCollection("testDB.foo");
        var coll1=mgs.getCollection("testDB.foo1");
        var testdb=mgs.getDB('testDB');
        st.startBalancer();
        assert.commandWorked(admin.runCommand({enableSharding:"testDB"}));
        assert.commandWorked(admin.runCommand({shardCollection:"testDB.foo",key:{a:1}}));
        jsTest.log("-------------------insert data-------------------");
        for (var i=0;i<1000;i++){
        assert.writeOK(coll.insert({a: i, c: i}));}
        printShardingStatus(st.config,false);
        jsTest.log("-------------------splite-------------------");
        
        //assert.commandFailed(cfg.runCommand({split: "testDB.fooo",manualsplit:true,find :{a : 1}}));
		assert.commandFailed(cfg.runCommand({split: "testDB.foo",middle :{a : 1}}));
		//assert.commandFailed(testdb.runCommand({split: "testDB.fooo",manualsplit:true,find :{a : 2}}));
		assert.commandFailed(testdb.runCommand({split: "testDB.foo",middle :{a : 2}}));
        printShardingStatus(st.config,false);
        jsTest.log("-------------------confirm chunks normal-------------------");
        var chunks = cfg.chunks.find().toArray();
        var num = cfg.chunks.find().itcount();
        assert.eq(num,1);
        st.stop();
})();
