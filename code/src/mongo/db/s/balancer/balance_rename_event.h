/**
 *    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include "mongo/base/disallow_copying.h"
#include "mongo/db/s/balancer/balance_event_engine.h"

namespace mongo {

// RenameEvent definition for rebalance state machine
class RenameEvent final : public IRebalanceEvent {

    MONGO_DISALLOW_COPYING(RenameEvent);

public:
    RenameEvent(const ChunkType& chunk,
                const std::string& toNS,
                IRebalanceEvent::EventResultInfo* result,
                bool dropTarget,
                bool stayTemp,
                bool userCommand = false,
                bool needWriteEvent = true);

    RenameEvent(const std::string& eventId, IRebalanceEvent::EventResultInfo* result);

    virtual ~RenameEvent();

    struct RenameEventInfo {
        BalanceEventEngine::RebalanceEventState curState;
        BalanceEventEngine::RebalanceEventState prevState;
        ChunkType chunk;
        std::string toNS;
        bool dropTarget;
        bool stayTemp;
    };

    // encode internal data into a BSONObj
    virtual BSONObj dataToBSON() override;

    // decode internal data from a BSONObj
    virtual Status dataFromBSON(const BSONObj& eventData) override;

    virtual void transitState(BalanceEventEngine::RebalanceEventState nextState);

    virtual void rollbackLastTransition();

    virtual const std::string& getEventId() const;

    // get chunk of this event
    virtual const ChunkType& getChunk() const;

    // get current state of this event
    virtual BalanceEventEngine::RebalanceEventState getState();

    // get previous state of this event
    virtual BalanceEventEngine::RebalanceEventState getPrevState();

    virtual bool getNewChunkFlag();
    virtual bool isInInitialState();

    virtual bool shouldContinue();

    virtual void refreshChunk(const ChunkType& chunk);
    virtual void refreshChunk(const ChunkType& pChunk, const ChunkType& cChunk);

    // get source shard server of this event
    const ShardIdent getSource() const;

    // get target shard server of this event
    const ShardIdent& getTarget() const;

    const std::string& getTargetCollection() const;

    const bool getDropTarget() const;

    const bool getStayTemp() const;

private:
    std::string _eventId;
    RenameEventInfo _renameEventInfo;
    BalanceEventEngine::RebalanceEventState _rollbackState =
        BalanceEventEngine::RebalanceEventState::kStartOffload;
    const BalanceEventEngine::RebalanceEventState _initState =
        BalanceEventEngine::RebalanceEventState::kStartOffload;
    const BalanceEventEngine::RebalanceEventState _finalState =
        BalanceEventEngine::RebalanceEventState::kAssigned;
};

}  // namespace mongo
