/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/multi_index_block.h"

#include "mongo/db/catalog/collection_mock.h"
#include "mongo/db/catalog/index_catalog_noop.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/service_context_test_fixture.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

/**
 * Unit test for MultiIndexBlock to verify basic functionality.
 * Using a mocked Collection object ensures that we are pulling in a minimal set of library
 * dependencies.
 * For integration tests, it may be necessary to make this test fixture inherit from
 * ServiceContextMongoDTest.
 */
class MultiIndexBlockTest : public ServiceContextTest {
private:
    void setUp() override;
    void tearDown() override;

protected:
    OperationContext* getOpCtx() const;
    Collection* getCollection() const;
    MultiIndexBlock* getIndexer() const;

private:
    ServiceContext::UniqueOperationContext _opCtx;
    std::unique_ptr<Collection> _collection;
    std::unique_ptr<MultiIndexBlock> _indexer;
};

void MultiIndexBlockTest::setUp() {
    ServiceContextTest::setUp();

    auto service = getServiceContext();
    repl::ReplicationCoordinator::set(service,
                                      std::make_unique<repl::ReplicationCoordinatorMock>(service));

    _opCtx = makeOperationContext();

    NamespaceString nss("mydb.mycoll");
    auto collectionMock =
        std::make_unique<CollectionMock>(nss, std::make_unique<IndexCatalogNoop>());
    _collection = std::move(collectionMock);

    _indexer = std::make_unique<MultiIndexBlock>();
}

void MultiIndexBlockTest::tearDown() {
    auto service = getServiceContext();
    repl::ReplicationCoordinator::set(service, {});

    _indexer = {};

    _collection = {};

    _opCtx = {};

    ServiceContextTest::tearDown();
}

OperationContext* MultiIndexBlockTest::getOpCtx() const {
    return _opCtx.get();
}

Collection* MultiIndexBlockTest::getCollection() const {
    return _collection.get();
}

MultiIndexBlock* MultiIndexBlockTest::getIndexer() const {
    return _indexer.get();
}

TEST_F(MultiIndexBlockTest, CommitWithoutInsertingDocuments) {
    auto indexer = getIndexer();

    auto specs = unittest::assertGet(indexer->init(
        getOpCtx(), getCollection(), std::vector<BSONObj>(), MultiIndexBlock::kNoopOnInitFn));
    ASSERT_EQUALS(0U, specs.size());

    ASSERT_OK(indexer->dumpInsertsFromBulk(getOpCtx()));
    ASSERT_OK(indexer->checkConstraints(getOpCtx()));

    {
        WriteUnitOfWork wunit(getOpCtx());
        ASSERT_OK(indexer->commit(getOpCtx(),
                                  getCollection(),
                                  MultiIndexBlock::kNoopOnCreateEachFn,
                                  MultiIndexBlock::kNoopOnCommitFn));
        wunit.commit();
    }
}

TEST_F(MultiIndexBlockTest, CommitAfterInsertingSingleDocument) {
    auto indexer = getIndexer();

    auto specs = unittest::assertGet(indexer->init(
        getOpCtx(), getCollection(), std::vector<BSONObj>(), MultiIndexBlock::kNoopOnInitFn));
    ASSERT_EQUALS(0U, specs.size());

    ASSERT_OK(indexer->insert(getOpCtx(), {}, {}));
    ASSERT_OK(indexer->dumpInsertsFromBulk(getOpCtx()));
    ASSERT_OK(indexer->checkConstraints(getOpCtx()));

    {
        WriteUnitOfWork wunit(getOpCtx());
        ASSERT_OK(indexer->commit(getOpCtx(),
                                  getCollection(),
                                  MultiIndexBlock::kNoopOnCreateEachFn,
                                  MultiIndexBlock::kNoopOnCommitFn));
        wunit.commit();
    }

    // abort() should have no effect after the index build is committed.
    indexer->abortIndexBuild(getOpCtx(), getCollection(), MultiIndexBlock::kNoopOnCleanUpFn);
}

TEST_F(MultiIndexBlockTest, AbortWithoutCleanupAfterInsertingSingleDocument) {
    auto indexer = getIndexer();
    auto specs = unittest::assertGet(indexer->init(
        getOpCtx(), getCollection(), std::vector<BSONObj>(), MultiIndexBlock::kNoopOnInitFn));
    ASSERT_EQUALS(0U, specs.size());
    ASSERT_OK(indexer->insert(getOpCtx(), {}, {}));
    indexer->abortWithoutCleanup(getOpCtx());
}

}  // namespace
}  // namespace mongo
