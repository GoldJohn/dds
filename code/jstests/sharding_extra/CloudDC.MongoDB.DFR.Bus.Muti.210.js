(function() {
    'use strict';
	for(var i=0;i<2;i++){
        var st = new ShardingTest({shards: 3, mongos: 1});
	var array=[0,1,2];
	var rdm=[];
	for (var j=0;j<2;j++){
	var index=Math.floor(Math.random()*array.length);
	rdm[j]=array.splice(index,1)[0];
	}
	rdm.sort(function(a,b){return a>b?1:-1});
	var sort1=rdm[0];
	var sort2=rdm[1];
	var array1=[0,1,2];
	var rdm1=[];
	for (var j=0;j<2;j++){
	var index=Math.floor(Math.random()*array1.length);
	rdm1[j]=array1.splice(index,1)[0];
	}
	var rss=rdm1[0];
	var rss1=rdm1[1];
	var primarycs=st.configRS.getPrimary();var configSecondaryList = st.configRS.getSecondaries();var mgs=st.s0;
        var admin=mgs.getDB('admin');
        var cfg=mgs.getDB('config');
        var coll=mgs.getCollection("testDB.foo");
        var coll1=mgs.getCollection("testDB.foo1");
        var testdb=mgs.getDB('testDB');   
        st.startBalancer();
        assert.commandWorked(admin.runCommand({enableSharding:"testDB"}));
        assert.commandWorked(admin.runCommand({shardCollection:"testDB.foo",key:{a:1}}));
        assert.writeOK(coll.insert({a: -10, c: 10},{ writeConcern: { w: 1,j:true}}));
	printShardingStatus(st.config,false);
		MongoRunner.stopMongod(st._configServers[sort1]);
		MongoRunner.stopMongod(st._configServers[sort2]);
		MongoRunner.stopMongod(st._connections[rss]);
		MongoRunner.stopMongod(st._connections[rss1]);
		//printShardingStatus(st.config,false);
		
		
		var array=[];
		for (var m=0;m<=(i%2);m++)
		{
			array[m]=m;
		}
		for (var n=0;n<array.length;n++)
		{
			MongoRunner.runMongod(st._configServers[rdm[n]]);
		}
		sleep (120 *1000);
		
			assert.eq(1,cfg.shards.find().itcount(),"shard counts is wrong");
			assert.commandWorked(admin.runCommand({shardCollection:"testDB.foo1",key:{b:1}}));
						assert.writeOK(coll1.insert({b: -20, d: 20},{ writeConcern: { w: 1,j:true}}));
				
		var shards = cfg.shards.find().toArray();
		var state1=shards[0].state;
		assert.eq(state1,1);
        jsTest.log("-------------------add shard OK-------------------");
		assert.commandWorked(admin.runCommand({moveChunk: "testDB.foo",find:{a: -10},to: shards[0]._id}));
        assert.writeOK(coll.update({c: 10,},{$set : {c : 102}}, false,true));
        assert.writeOK(coll1.update({d: 20,},{$set : {d : 22}}, false,true));
        assert.eq(102,coll.find({a:-10}).toArray()[0].c, "update 4 failed");
        assert.eq(22,coll1.find({b:-20}).toArray()[0].d, "update 5 failed");
        jsTest.log("-------------------update coll OK!2!-------------------");
        assert.eq(1, coll.find().itcount());
        assert.eq(1, coll1.find().itcount());
        assert.neq(null, coll.getIndexes());
			
	
	st.stop();
	}
	
})();	
