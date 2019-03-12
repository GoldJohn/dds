"""
Replica set fixture for executing JSTests against.
"""

from __future__ import absolute_import

import os.path
import time
import shutil

import pymongo

from . import interface
from . import standalone
from ... import config
from ... import logging
from ... import utils


class ReplicaSetFixture(interface.ReplFixture):
    """
    Fixture which provides JSTests with a replica set to run against.
    """

    def __init__(self,
                 logger,
                 job_num,
                 mongod_executable=None,
                 mongod_options=None,
                 dbpath_prefix=None,
                 preserve_dbpath=False,
                 num_nodes=2,
                 start_initial_sync_node=False,
                 write_concern_majority_journal_default=None,
                 auth_options=None,
                 replset_config_options=None):

        interface.ReplFixture.__init__(self, logger, job_num)

        self.mongod_executable = mongod_executable
        self.mongod_options = utils.default_if_none(mongod_options, {})
        self.preserve_dbpath = preserve_dbpath
        self.num_nodes = num_nodes
        self.start_initial_sync_node = start_initial_sync_node
        self.write_concern_majority_journal_default = write_concern_majority_journal_default
        self.auth_options = auth_options
        self.replset_config_options = utils.default_if_none(replset_config_options, {})

        # The dbpath in mongod_options is used as the dbpath prefix for replica set members and
        # takes precedence over other settings. The ShardedClusterFixture uses this parameter to
        # create replica sets and assign their dbpath structure explicitly.
        if "dbpath" in self.mongod_options:
            self._dbpath_prefix = self.mongod_options.pop("dbpath")
        else:
            # Command line options override the YAML configuration.
            dbpath_prefix = utils.default_if_none(config.DBPATH_PREFIX, dbpath_prefix)
            dbpath_prefix = utils.default_if_none(dbpath_prefix, config.DEFAULT_DBPATH_PREFIX)
            self._dbpath_prefix = os.path.join(dbpath_prefix,
                                               "job%d" % (self.job_num),
                                               config.FIXTURE_SUBDIR)

        self.nodes = []
        self.replset_name = None
        self.initial_sync_node = None
        self.initial_sync_node_idx = -1

    def setup(self, logflag=""):
        shutil.rmtree(self._dbpath_prefix, ignore_errors=True)
        #os.popen('/root/namenode/hadoop-2.7.3/bin/hdfs dfs -rm -r hdfs://163.40.13.205:9000' + self._dbpath_prefix + '/*')
        #time.sleep(2)
        self.replset_name = self.mongod_options.get("replSet", "rs")

        if not self.nodes:
            for i in xrange(self.num_nodes):
                node = self._new_mongod(i, self.replset_name)
                self.nodes.append(node)

        i = 1
        for node in self.nodes:
            node.setup(logflag + "_" + self.replset_name + str(i))
            i += 1

        if self.start_initial_sync_node:
            if not self.initial_sync_node:
                self.initial_sync_node_idx = len(self.nodes)
                self.initial_sync_node = self._new_mongod(self.initial_sync_node_idx,
                                                          self.replset_name)
            self.initial_sync_node.setup(logflag)
            self.initial_sync_node.await_ready()

        self.port = self.get_primary().port

        # Call await_ready() on each of the nodes here because we want to start the election as
        # soon as possible.
        for node in self.nodes:
            node.await_ready()

        # Initiate the replica set.
        members = []
        for (i, node) in enumerate(self.nodes):
            member_info = {"_id": i, "host": node.get_connection_string()}
            if i > 0:
                member_info["priority"] = 0
            if i >= 7:
                # Only 7 nodes in a replica set can vote, so the other members must be non-voting.
                member_info["votes"] = 0
            members.append(member_info)
        if self.initial_sync_node:
            members.append({"_id": self.initial_sync_node_idx,
                            "host": self.initial_sync_node.get_connection_string(),
                            "priority": 0,
                            "hidden": 1,
                            "votes": 0})

        initiate_cmd_obj = {"replSetInitiate": {"_id": self.replset_name, "members": members}}

        if self.write_concern_majority_journal_default is not None:
            initiate_cmd_obj["replSetInitiate"]["writeConcernMajorityJournalDefault"] = self.write_concern_majority_journal_default

        client = utils.new_mongo_client(port=self.port)
        if self.auth_options is not None:
            auth_db = client[self.auth_options["authenticationDatabase"]]
            auth_db.authenticate(self.auth_options["username"],
                                 password=self.auth_options["password"],
                                 mechanism=self.auth_options["authenticationMechanism"])

        if self.replset_config_options.get("configsvr", False):
            initiate_cmd_obj["replSetInitiate"]["configsvr"] = True
        if "protocolVersion" in self.replset_config_options:
            protocol_version = self.replset_config_options["protocolVersion"]
            initiate_cmd_obj["replSetInitiate"]["protocolVersion"] = protocol_version
        if self.replset_config_options.get("settings"):
            replset_settings = self.replset_config_options["settings"]
            initiate_cmd_obj["replSetInitiate"]["settings"] = replset_settings

        self.logger.info("Issuing replSetInitiate command...%s", initiate_cmd_obj)
        client.admin.command(initiate_cmd_obj)

    def await_ready(self):
        # Wait for the primary to be elected.
        client = utils.new_mongo_client(port=self.port)
        while True:
            is_master = client.admin.command("isMaster")["ismaster"]
            if is_master:
                break
            self.logger.info("Waiting for primary on port %d to be elected.", self.port)
            time.sleep(0.1)  # Wait a little bit before trying again.

        secondaries = self.get_secondaries()
        if self.initial_sync_node:
            secondaries.append(self.initial_sync_node)

        # Wait for the secondaries to become available.
        for secondary in secondaries:
            client = utils.new_mongo_client(port=secondary.port,
                                            read_preference=pymongo.ReadPreference.SECONDARY)
            while True:
                is_secondary = client.admin.command("isMaster")["secondary"]
                if is_secondary:
                    break
                self.logger.info("Waiting for secondary on port %d to become available.",
                                 secondary.port)
                time.sleep(0.1)  # Wait a little bit before trying again.

    def teardown(self):
        running_at_start = self.is_running()
        success = True  # Still a success even if nothing is running.

        if not running_at_start:
            self.logger.info("Replica set was expected to be running in teardown(), but wasn't.")
        else:
            self.logger.info("Stopping all members of the replica set...")

        if self.initial_sync_node:
            success = self.initial_sync_node.teardown() and success

        # Terminate the secondaries first to reduce noise in the logs.
        for node in reversed(self.nodes):
            success = node.teardown() and success

        if running_at_start:
            self.logger.info("Successfully stopped all members of the replica set.")

        return success

    def is_running(self):
        running = all(node.is_running() for node in self.nodes)

        if self.initial_sync_node:
            running = self.initial_sync_node.is_running() or running

        return running

    def get_primary(self):
        # The primary is always the first element of the 'nodes' list because all other members of
        # the replica set are configured with priority=0.
        return self.nodes[0]

    def get_secondaries(self):
        return self.nodes[1:]

    def get_initial_sync_node(self):
        return self.initial_sync_node

    def _new_mongod(self, index, replset_name):
        """
        Returns a standalone.MongoDFixture configured to be used as a
        replica-set member of 'replset_name'.
        """

        mongod_logger = self._get_logger_for_mongod(index)
        mongod_options = self.mongod_options.copy()
        mongod_options["replSet"] = replset_name
        mongod_options["dbpath"] = os.path.join(self._dbpath_prefix, "node%d" % (index))

        return standalone.MongoDFixture(mongod_logger,
                                        self.job_num,
                                        mongod_executable=self.mongod_executable,
                                        mongod_options=mongod_options,
                                        preserve_dbpath=self.preserve_dbpath)

    def _get_logger_for_mongod(self, index):
        """
        Returns a new logging.Logger instance for use as the primary, secondary, or initial
        sync member of a replica-set.
        """

        if index == 0:
            logger_name = "%s:primary" % (self.logger.name)
        elif index == self.initial_sync_node_idx:
            logger_name = "%s:initsync" % (self.logger.name)
        else:
            suffix = str(index - 1) if self.num_nodes > 2 else ""
            logger_name = "%s:secondary%s" % (self.logger.name, suffix)

        return logging.loggers.new_logger(logger_name, parent=self.logger)

    def get_connection_string(self):
        if self.replset_name is None:
            raise ValueError("Must call setup() before calling get_connection_string()")

        conn_strs = [node.get_connection_string() for node in self.nodes]
        if self.initial_sync_node:
            conn_strs.append(self.initial_sync_node.get_connection_string())
        return self.replset_name + "/" + ",".join(conn_strs)
