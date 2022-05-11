/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/connectors/tpch/TpchConnector.h"
#include <folly/init/Init.h>
#include "gtest/gtest.h"
#include "velox/exec/tests/utils/OperatorTestBase.h"
#include "velox/exec/tests/utils/PlanBuilder.h"

namespace {

using namespace facebook::velox;
using namespace facebook::velox::connector::tpch;

using facebook::velox::exec::test::PlanBuilder;
using facebook::velox::tpch::Table;

class TpchConnectorTest : public exec::test::OperatorTestBase {
 public:
  const std::string kTpchConnectorId = "test-tpch";

  void SetUp() override {
    OperatorTestBase::SetUp();
    auto tpchConnector =
        connector::getConnectorFactory(
            connector::tpch::TpchConnectorFactory::kTpchConnectorName)
            ->newConnector(kTpchConnectorId, nullptr);
    connector::registerConnector(tpchConnector);
  }

  void TearDown() override {
    connector::unregisterConnector(kTpchConnectorId);
    OperatorTestBase::TearDown();
  }

  exec::Split makeTpchSplit() const {
    return exec::Split(std::make_shared<TpchConnectorSplit>(kTpchConnectorId));
  }

  // Helper function to create 1:1 assignments maps based on output type.
  auto defaultAssignments(const RowTypePtr& outputType) const {
    std::unordered_map<std::string, std::shared_ptr<connector::ColumnHandle>>
        assignmentsMap;

    for (const auto& columnName : outputType->names()) {
      assignmentsMap.emplace(
          columnName, std::make_shared<TpchColumnHandle>(columnName));
    }
    return assignmentsMap;
  }

  void runScaleFactorTest(size_t scaleFactor);
};

// Simple scan of first 5 rows of "nation".
TEST_F(TpchConnectorTest, simple) {
  auto outputType =
      ROW({"n_nationkey", "n_name", "n_regionkey", "n_comment"},
          {BIGINT(), VARCHAR(), BIGINT(), VARCHAR()});
  auto plan = PlanBuilder()
                  .tableScan(
                      outputType,
                      std::make_shared<TpchTableHandle>(Table::TBL_NATION),
                      defaultAssignments(outputType))
                  .limit(0, 5, false)
                  .planNode();

  auto output = getResults(plan, {makeTpchSplit()});
  auto expected = makeRowVector({
      // n_nationkey
      makeFlatVector<int64_t>({0, 1, 2, 3, 4}),
      // n_name
      makeFlatVector<StringView>({
          "ALGERIA",
          "ARGENTINA",
          "BRAZIL",
          "CANADA",
          "EGYPT",
      }),
      // n_regionkey
      makeFlatVector<int64_t>({0, 1, 1, 1, 4}),
      // n_comment
      makeFlatVector<StringView>({
          " haggle. carefully final deposits detect slyly agai",
          "al foxes promise slyly according to the regular accounts. bold requests alon",
          "y alongside of the pending deposits. carefully special packages are about the ironic forges. slyly special ",
          "eas hang ironic, silent packages. slyly regular packages are furiously over the tithes. fluffily bold",
          "y above the carefully unusual theodolites. final dugouts are quickly across the furiously regular d",
      }),
  });
  test::assertEqualVectors(expected, output);
}

// Extract single column from "nation".
TEST_F(TpchConnectorTest, singleColumn) {
  auto outputType = ROW({"n_name"}, {VARCHAR()});
  auto plan = PlanBuilder()
                  .tableScan(
                      outputType,
                      std::make_shared<TpchTableHandle>(Table::TBL_NATION),
                      defaultAssignments(outputType))
                  .planNode();

  auto output = getResults(plan, {makeTpchSplit()});
  auto expected = makeRowVector({makeFlatVector<StringView>({
      "ALGERIA",       "ARGENTINA", "BRAZIL", "CANADA",
      "EGYPT",         "ETHIOPIA",  "FRANCE", "GERMANY",
      "INDIA",         "INDONESIA", "IRAN",   "IRAQ",
      "JAPAN",         "JORDAN",    "KENYA",  "MOROCCO",
      "MOZAMBIQUE",    "PERU",      "CHINA",  "ROMANIA",
      "SAUDI ARABIA",  "VIETNAM",   "RUSSIA", "UNITED KINGDOM",
      "UNITED STATES",
  })});
  test::assertEqualVectors(expected, output);
  EXPECT_EQ("n_name", output->type()->asRow().nameOf(0));
}

// Check that aliases are correctly resolved.
TEST_F(TpchConnectorTest, singleColumnWithAlias) {
  const std::string aliasedName = "my_aliased_column_name";

  auto outputType = ROW({aliasedName}, {VARCHAR()});
  auto plan =
      PlanBuilder()
          .tableScan(
              outputType,
              std::make_shared<TpchTableHandle>(Table::TBL_NATION),
              {
                  {aliasedName, std::make_shared<TpchColumnHandle>("n_name")},
                  {"other_name", std::make_shared<TpchColumnHandle>("n_name")},
                  {"third_column",
                   std::make_shared<TpchColumnHandle>("n_regionkey")},
              })
          .limit(0, 1, false)
          .planNode();

  auto output = getResults(plan, {makeTpchSplit()});
  auto expected = makeRowVector({makeFlatVector<StringView>({
      "ALGERIA",
  })});
  test::assertEqualVectors(expected, output);

  EXPECT_EQ(aliasedName, output->type()->asRow().nameOf(0));
  EXPECT_EQ(1, output->childrenSize());
}

void TpchConnectorTest::runScaleFactorTest(size_t scaleFactor) {
  auto plan = PlanBuilder()
                  .tableScan(
                      ROW({}, {}),
                      std::make_shared<TpchTableHandle>(
                          Table::TBL_SUPPLIER, scaleFactor),
                      {})
                  .singleAggregation({}, {"count(1)"})
                  .planNode();

  auto output = getResults(plan, {makeTpchSplit()});
  int64_t expectedRows =
      tpch::getRowCount(tpch::Table::TBL_SUPPLIER, scaleFactor);
  auto expected = makeRowVector(
      {makeFlatVector<int64_t>(std::vector<int64_t>{expectedRows})});
  test::assertEqualVectors(expected, output);
}

// Aggregation over a larger table.
TEST_F(TpchConnectorTest, simpleAggregation) {
  runScaleFactorTest(1);
  runScaleFactorTest(5);
  runScaleFactorTest(13);
}

TEST_F(TpchConnectorTest, unknownColumn) {
  auto outputType = ROW({"does_not_exist"}, {VARCHAR()});
  auto plan = PlanBuilder()
                  .tableScan(
                      outputType,
                      std::make_shared<TpchTableHandle>(Table::TBL_NATION),
                      defaultAssignments(outputType))
                  .planNode();

  EXPECT_THROW(getResults(plan, {makeTpchSplit()}), VeloxRuntimeError);
}

} // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  folly::init(&argc, &argv, false);
  return RUN_ALL_TESTS();
}