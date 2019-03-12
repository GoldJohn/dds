/**
 *    Copyright (C) 2012-2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <boost/optional.hpp>
#include <string>

#include "mongo/bson/bsonobj.h"
#include "mongo/s/chunk_version.h"
#include "mongo/s/shard_id.h"

namespace mongo {

class BSONObjBuilder;
class Status;
template <typename T>
class StatusWith;

/**
 * Contains the minimum representation of a chunk - its bounds in the format [min, max) along with
 * utilities for parsing and persistence.
 */
class ChunkRange {
public:
    ChunkRange(BSONObj minKey, BSONObj maxKey);

    /**
     * Parses a chunk range using the format { min: <min bound>, max: <max bound> }.
     */
    static StatusWith<ChunkRange> fromBSON(const BSONObj& obj);

    const BSONObj& getMin() const {
        return _minKey;
    }

    const BSONObj& getMax() const {
        return _maxKey;
    }

    /**
     * Checks whether the specified key is within the bounds of this chunk range.
     */
    bool containsKey(const BSONObj& key) const;

    /**
     * Writes the contents of this chunk range as { min: <min bound>, max: <max bound> }.
     */
    void append(BSONObjBuilder* builder) const;

    std::string toString() const;

    /**
     * Returns true if two chunk ranges match exactly in terms of the min and max keys (including
     * element order within the keys).
     */
    bool operator==(const ChunkRange& other) const;
    bool operator!=(const ChunkRange& other) const;

private:
    BSONObj _minKey;
    BSONObj _maxKey;
};

/**
 * This class represents the layout and contents of documents contained in the
 * config.chunks collection. All manipulation of documents coming from that
 * collection should be done with this class.
 */
class ChunkType {
public:
    // the width of chunkID,
    static const int kChunkIDDigitWidth;

    enum class ChunkStatus : int {
        kOffloaded = 0,
        kAssigned,
        kDisabled,                // Chunk will not be processed by balancer
        kFirstInvalidChunkStatus  // make it easy to do the sanity check when adding new status
    };

    // Name of the chunks collection in the config server.
    static const std::string ConfigNS;

    // Field names and types in the chunks collection type.
    static const BSONField<std::string> name;
    static const BSONField<std::string> ns;
    static const BSONField<BSONObj> min;
    static const BSONField<BSONObj> max;
    static const BSONField<std::string> shard;
    static const BSONField<std::string> processIdentity;
    static const BSONField<bool> jumbo;
    static const BSONField<Date_t> DEPRECATED_lastmod;
    static const BSONField<OID> DEPRECATED_epoch;
    static const BSONField<ChunkStatus> status;
    // When using persistent layer as the underlying storage, it means a rootploglist,
    // when using HDFS as the underlying storage, it means a HDFS path.
    static const BSONField<std::string> rootFolder;

    /**
     * Constructs a new ChunkType object from BSON.
     * Also does validation of the contents.
     */
    static StatusWith<ChunkType> fromBSON(const BSONObj& source);

    /**
     * Generates chunk id based on the namespace name and the lower bound of the chunk.
     */
    static std::string genID(StringData ns, const BSONObj& min);

    // Sanity check the status
    static bool isStatusValid(ChunkStatus status) {
        return (status >= ChunkStatus::kOffloaded &&
                status < ChunkStatus::kFirstInvalidChunkStatus);
    }

    // add prefix "000000000" for chunkId
    static std::string toID(const std::string& id);

    /**
     * Returns OK if all fields have been set. Otherwise returns NoSuchKey
     * and information about the first field that is missing.
     */
    Status validate() const;

    /**
     * Returns the BSON representation of the entry.
     */
    BSONObj toBSON() const;

    /**
     * Returns a std::string representation of the current internal state.
     */
    std::string toString() const;

    // get id of the chunk without leading 0
    std::string getName() const;
    void setName(const std::string& id);

    const std::string& getNS() const {
        return _ns.get();
    }

    std::string getFullNs() const {
        std::string ns_with_chunkID = getNS() + '$' + getName();
        return ns_with_chunkID;
    }

    void setNS(const std::string& name);

    const BSONObj& getMin() const {
        return _min.get();
    }
    void setMin(const BSONObj& min);

    const BSONObj& getMax() const {
        return _max.get();
    }
    void setMax(const BSONObj& max);

    bool isVersionSet() const {
        return _version.is_initialized();
    }
    const ChunkVersion& getVersion() const {
        return _version.get();
    }
    void setVersion(const ChunkVersion& version);

    const ShardId& getShard() const {
        return _shard.get();
    }
    void setShard(const ShardId& shard);

    bool getJumbo() const {
        return _jumbo.get_value_or(false);
    }
    void setJumbo(bool jumbo);

    ChunkStatus getStatus() const {
        return _status;
    }
    void setStatus(const ChunkStatus status);

    const std::string& getRootFolder() const {
        return _rootFolder.get();
    }

    std::string getRelativeRootFolder() const;

    void setRootFolder(const std::string& rootFolder);
    void clearRootFolder();

    // get the full id field
    const std::string& getID() const {
        return _id;
    }

    void setID(const std::string& id) {
        _id = id;
    }

    bool isAssigned() const {
        return (_status == ChunkStatus::kAssigned);
    }

    void setProcessIdentity(const std::string& processIdentity);

    const std::string& getProcessIdentity() const {
        return _processIdentity;
    }

    std::string getCollectionIdent() const;

    static std::string toSex(uint64_t id);
    static std::string widthChunkID(const std::string& chunkId);

    std::string getChunkDataPath(void) const;

private:
    // Convention: (M)andatory, (O)ptional, (S)pecial rule.

    // (M)  collection this chunk is in
    boost::optional<std::string> _ns;
    // (M)  first key of the range, inclusive
    boost::optional<BSONObj> _min;
    // (M)  last key of the range, non-inclusive
    boost::optional<BSONObj> _max;
    // (M)  version of this chunk
    boost::optional<ChunkVersion> _version;
    // (M)  shard this chunk lives in
    boost::optional<ShardId> _shard;
    // (O)  too big to move?
    boost::optional<bool> _jumbo;
    // (M) chunk status
    ChunkStatus _status = ChunkStatus::kOffloaded;
    // (O) chunk's root folder which could be rootploglist or hdfs path
    boost::optional<std::string> _rootFolder;
    // (M)  unique chunkID
    std::string _id = "";
    std::string _processIdentity = "noidentity";
};

}  // namespace mongo
