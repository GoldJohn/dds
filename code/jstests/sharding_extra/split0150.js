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
			 var b=Number(i-100);
        assert.writeOK(coll.insert({a: b, c: b}));}
        printShardingStatus(st.config,false);
        jsTest.log("-------------------splite-------------------");
		var minn=Number(0.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001)
        //assert.commandWorked(admin.runCommand({split: "testDB.foo",manualsplit:true,find :{a : minn}}));
		assert.commandWorked(admin.runCommand({split: "testDB.foo",middle :{a : minn}}));
        printShardingStatus(st.config,false);
        jsTest.log("-------------------confirm chunks normal-------------------");
        var chunks = cfg.chunks.find().toArray();
        var max = chunks[0].max.a;
		var min = chunks[1].min.a;
        assert.eq(max,minn);
		assert.eq(min,minn);
        var chunks = cfg.chunks.find().toArray();
        var num = cfg.chunks.find().itcount();
        assert.eq(num,2);
        jsTest.log("-------------------confirm update normal-------------------");
        var ransl = minn - 100;
        var ransr = minn + 100;
        assert.writeOK(coll.insert({a: ransl,c: 1001}));
        assert.writeOK(coll.insert({a: ransr,c: 1002}));
        assert.commandWorked(admin.runCommand({shardCollection:"testDB.foo1",key:{b:1}}));
        assert.writeOK(coll1.insert({b: 10, d: 20}));
        assert.writeOK(coll.update({c: -1},{$set : {c : 1003}}, false,true));
        assert.writeOK(coll.update({c: 100},{$set : {c : 1004}}, false,true));
        assert.eq(1003,coll.find({a: -1}).toArray()[0].c, "update  failed");
        assert.eq(1004,coll.find({a: 100}).toArray()[0].c, "update  failed");
        st.stop();
})();
