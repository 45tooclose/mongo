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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/repl/data_replicator_external_state_mock.h"

#include "mongo/db/repl/oplog_buffer_blocking_queue.h"
#include "mongo/stdx/memory.h"

namespace mongo {
namespace repl {

DataReplicatorExternalStateMock::DataReplicatorExternalStateMock()
    : multiApplyFn([](OperationContext*,
                      const MultiApplier::Operations& ops,
                      OplogApplier::Observer*) { return ops.back().getOpTime(); }) {}

executor::TaskExecutor* DataReplicatorExternalStateMock::getTaskExecutor() const {
    return taskExecutor;
}

OpTimeWithTerm DataReplicatorExternalStateMock::getCurrentTermAndLastCommittedOpTime() {
    return {currentTerm, lastCommittedOpTime};
}

void DataReplicatorExternalStateMock::processMetadata(
    const rpc::ReplSetMetadata& replMetadata, boost::optional<rpc::OplogQueryMetadata> oqMetadata) {
    replMetadataProcessed = replMetadata;
    if (oqMetadata) {
        oqMetadataProcessed = oqMetadata.get();
    }
    metadataWasProcessed = true;
}

bool DataReplicatorExternalStateMock::shouldStopFetching(
    const HostAndPort& source,
    const rpc::ReplSetMetadata& replMetadata,
    boost::optional<rpc::OplogQueryMetadata> oqMetadata) {
    lastSyncSourceChecked = source;

    // If OplogQueryMetadata was provided, use its values, otherwise use the ones in
    // ReplSetMetadata.
    if (oqMetadata) {
        syncSourceLastOpTime = oqMetadata->getLastOpApplied();
        syncSourceHasSyncSource = oqMetadata->getSyncSourceIndex() != -1;
    } else {
        syncSourceLastOpTime = replMetadata.getLastOpVisible();
        syncSourceHasSyncSource = replMetadata.getSyncSourceIndex() != -1;
    }
    return shouldStopFetchingResult;
}

std::unique_ptr<OplogBuffer> DataReplicatorExternalStateMock::makeInitialSyncOplogBuffer(
    OperationContext* opCtx) const {
    return stdx::make_unique<OplogBufferBlockingQueue>();
}

StatusWith<OplogApplier::Operations> DataReplicatorExternalStateMock::getNextApplierBatch(
    OperationContext* opCtx, OplogBuffer* oplogBuffer) {
    OplogApplier::Operations ops;
    OplogBuffer::Value op;
    // For testing only. Return a single batch containing all of the operations in the oplog buffer.
    while (oplogBuffer->tryPop(opCtx, &op)) {
        OplogEntry entry(op);
        // The "InitialSyncerPassesThroughGetNextApplierBatchInLockError" test case expects
        // ErrorCodes::BadValue on an unexpected oplog entry version.
        if (entry.getVersion() != OplogEntry::kOplogVersion) {
            return {ErrorCodes::BadValue, ""};
        }
        ops.push_back(entry);
    }
    return std::move(ops);
}

StatusWith<ReplSetConfig> DataReplicatorExternalStateMock::getCurrentConfig() const {
    return replSetConfigResult;
}

StatusWith<OpTime> DataReplicatorExternalStateMock::_multiApply(OperationContext* opCtx,
                                                                MultiApplier::Operations ops,
                                                                OplogApplier::Observer* observer,
                                                                const HostAndPort& source,
                                                                ThreadPool* writerPool) {
    return multiApplyFn(opCtx, std::move(ops), observer);
}

}  // namespace repl
}  // namespace mongo
