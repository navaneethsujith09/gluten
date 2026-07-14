/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <Parser/FunctionParser.h>

namespace DB
{
namespace ErrorCodes
{
extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
}
}

namespace local_engine
{
class FunctionParserSplitPart : public FunctionParser
{
public:
    explicit FunctionParserSplitPart(ParserContextPtr parser_context_) : FunctionParser(parser_context_) { }
    ~FunctionParserSplitPart() override = default;

    static constexpr auto name = "split_part";
    String getName() const override { return name; }

    const DB::ActionsDAG::Node * parse(
        const substrait::Expression_ScalarFunction & substrait_func, DB::ActionsDAG & actions_dag) const override
    {
        /*
            parse split_part(str, delim, partNum) as
            if (isNull(str) || isNull(delim) || isNull(partNum))
                null
            else if (partNum == 0)
                throwIf(partNum == 0, 'The index 0 is invalid. An index shall start from 1 or -1.')
            else if (empty(delim))
                if (partNum == 1) str else ''
            else
                arrayElement(splitByString(delim, str), partNum)
        */
        auto parsed_args = parseFunctionArguments(substrait_func, actions_dag);
        if (parsed_args.size() != 3)
            throw DB::Exception(DB::ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH, "Function {} requires exactly three arguments", getName());

        const auto * str_arg = parsed_args[0];
        const auto * delim_arg = parsed_args[1];
        const auto * part_num_arg = parsed_args[2];

        const auto nullable_string_type = DB::makeNullable(std::make_shared<DB::DataTypeString>());
        const auto * null_const_node = addColumnToActionsDAG(actions_dag, nullable_string_type, DB::Field{});
        const auto * empty_string_node = addColumnToActionsDAG(actions_dag, std::make_shared<DB::DataTypeString>(), "");
        const auto * zero_node = addColumnToActionsDAG(actions_dag, std::make_shared<DB::DataTypeInt32>(), 0);
        const auto * one_node = addColumnToActionsDAG(actions_dag, std::make_shared<DB::DataTypeInt32>(), 1);

        const auto * equal_zero_node = toFunctionNode(actions_dag, "equals", {part_num_arg, zero_node});
        const auto * throw_message = addColumnToActionsDAG(
            actions_dag,
            std::make_shared<DB::DataTypeString>(),
            "The index 0 is invalid. An index shall start from 1 or -1.");
        const auto * throw_if_node = toFunctionNode(actions_dag, "throwIf", {equal_zero_node, throw_message});

        const auto * empty_delim_node = toFunctionNode(actions_dag, "empty", {delim_arg});
        const auto * part_is_one_node = toFunctionNode(actions_dag, "equals", {part_num_arg, one_node});
        const auto * empty_delim_part_one_node = toFunctionNode(actions_dag, "and", {empty_delim_node, part_is_one_node});

        const auto * split_node = toFunctionNode(actions_dag, "splitByString", {delim_arg, str_arg});
        const auto * array_element_node = toFunctionNode(actions_dag, "arrayElement", {split_node, part_num_arg});

        const auto * empty_delim_result_node
            = toFunctionNode(actions_dag, "if", {empty_delim_part_one_node, str_arg, empty_string_node});
        const auto * non_null_result_node
            = toFunctionNode(actions_dag, "if", {empty_delim_node, empty_delim_result_node, array_element_node});
        const auto * result_with_throw_node
            = toFunctionNode(actions_dag, "if", {throw_if_node, non_null_result_node, non_null_result_node});

        const auto * str_is_null_node = toFunctionNode(actions_dag, "isNull", {str_arg});
        const auto * delim_is_null_node = toFunctionNode(actions_dag, "isNull", {delim_arg});
        const auto * part_is_null_node = toFunctionNode(actions_dag, "isNull", {part_num_arg});
        const auto * delim_or_part_null_node = toFunctionNode(actions_dag, "or", {delim_is_null_node, part_is_null_node});
        const auto * any_null_node = toFunctionNode(actions_dag, "or", {str_is_null_node, delim_or_part_null_node});

        const auto * final_node = toFunctionNode(actions_dag, "if", {any_null_node, null_const_node, result_with_throw_node});
        return convertNodeTypeIfNeeded(substrait_func, final_node, actions_dag);
    }
};

static FunctionParserRegister<FunctionParserSplitPart> register_split_part;
}
