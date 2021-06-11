// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "runtime/datetime_value.h"
#include "vec/columns/column_vector.h"
#include "vec/data_types/data_type_date.h"
#include "vec/data_types/data_type_date_time.h"
#include "vec/data_types/data_type_number.h"
#include "vec/functions/function.h"
#include "vec/functions/function_helpers.h"

namespace doris::vectorized {

namespace ErrorCodes {
extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
extern const int ILLEGAL_TYPE_OF_ARGUMENT;
extern const int ILLEGAL_COLUMN;
} // namespace ErrorCodes

template <TimeUnit unit>
inline Int128 date_time_add(const Int128& t, Int64 delta) {
    auto res = t;
    auto& ts_value = (doris::DateTimeValue&)(res);
    TimeInterval interval(unit, delta, false);
    ts_value.date_add_interval(interval, unit);

    return res;
}

#define ADD_TIME_FUNCTION_IMPL(CLASS, NAME, UNIT)                    \
    struct CLASS {                                                   \
        using ReturnType = DataTypeDateTime;                         \
        static constexpr auto name = #NAME;                          \
        static inline Int128 execute(const Int128& t, Int64 delta) { \
            return date_time_add<TimeUnit::UNIT>(t, delta);          \
        }                                                            \
    }

ADD_TIME_FUNCTION_IMPL(AddSecondsImpl, seconds_add, SECOND);
ADD_TIME_FUNCTION_IMPL(AddMinutesImpl, minutes_add, MINUTE);
ADD_TIME_FUNCTION_IMPL(AddHoursImpl, hours_add, HOUR);
ADD_TIME_FUNCTION_IMPL(AddDaysImpl, days_add, DAY);
ADD_TIME_FUNCTION_IMPL(AddWeeksImpl, weeks_add, WEEK);
ADD_TIME_FUNCTION_IMPL(AddMonthsImpl, months_add, MONTH);
ADD_TIME_FUNCTION_IMPL(AddYearsImpl, years_add, YEAR);

struct AddQuartersImpl {
    using ReturnType = DataTypeDateTime;
    static constexpr auto name = "quarters_add";

    static inline Int128 execute(const Int128& t, Int64 delta) {
        return date_time_add<TimeUnit::MONTH>(t, delta * 3);
    }
};

template <typename Transform>
struct SubtractIntervalImpl {
    using ReturnType = DataTypeDateTime;
    static inline Int128 execute(const Int128& t, Int64 delta) {
        return Transform::execute(t, -delta);
    }
};

struct SubtractSecondsImpl : SubtractIntervalImpl<AddSecondsImpl> {
    static constexpr auto name = "seconds_sub";
};
struct SubtractMinutesImpl : SubtractIntervalImpl<AddMinutesImpl> {
    static constexpr auto name = "minutes_sub";
};
struct SubtractHoursImpl : SubtractIntervalImpl<AddHoursImpl> {
    static constexpr auto name = "hours_sub";
};
struct SubtractDaysImpl : SubtractIntervalImpl<AddDaysImpl> {
    static constexpr auto name = "days_sub";
};
struct SubtractWeeksImpl : SubtractIntervalImpl<AddWeeksImpl> {
    static constexpr auto name = "weeks_sub";
};
struct SubtractMonthsImpl : SubtractIntervalImpl<AddMonthsImpl> {
    static constexpr auto name = "months_sub";
};
struct SubtractQuartersImpl : SubtractIntervalImpl<AddQuartersImpl> {
    static constexpr auto name = "quarters_sub";
};
struct SubtractYearsImpl : SubtractIntervalImpl<AddYearsImpl> {
    static constexpr auto name = "years_sub";
};

struct DateDiffImpl {
    using ReturnType = DataTypeInt32;
    static constexpr auto name = "datediff";

    static inline Int32 execute(const Int128& t0, const Int128& t1) {
        const auto& ts0 = reinterpret_cast<const doris::DateTimeValue&>(t0);
        const auto& ts1 = reinterpret_cast<const doris::DateTimeValue&>(t1);
        return ts0.daynr() - ts1.daynr();
    }
};

struct TimeDiffImpl {
    using ReturnType = DataTypeFloat64;
    static constexpr auto name = "timediff";

    static inline double execute(const Int128& t0, const Int128& t1) {
        const auto& ts0 = reinterpret_cast<const doris::DateTimeValue&>(t0);
        const auto& ts1 = reinterpret_cast<const doris::DateTimeValue&>(t1);
        return ts0.second_diff(ts1);
    }
};

#define TIME_DIFF_FUNCTION_IMPL(CLASS, NAME, UNIT)                               \
    struct CLASS {                                                               \
        using ReturnType = DataTypeInt64;                                        \
        static constexpr auto name = #NAME;                                      \
        static inline int64_t execute(const Int128& t0, const Int128& t1) {      \
            const auto& ts0 = reinterpret_cast<const doris::DateTimeValue&>(t0); \
            const auto& ts1 = reinterpret_cast<const doris::DateTimeValue&>(t1); \
            return DateTimeValue::datetime_diff<TimeUnit::UNIT>(ts1, ts0);       \
        }                                                                        \
    }

TIME_DIFF_FUNCTION_IMPL(YearsDiffImpl, years_diff, YEAR);
TIME_DIFF_FUNCTION_IMPL(MonthsDiffImpl, months_diff, MONTH);
TIME_DIFF_FUNCTION_IMPL(WeeksDiffImpl, weeks_diff, WEEK);
TIME_DIFF_FUNCTION_IMPL(DaysDiffImpl, days_diff, DAY);
TIME_DIFF_FUNCTION_IMPL(HoursDiffImpl, hours_diff, HOUR);
TIME_DIFF_FUNCTION_IMPL(MintueSDiffImpl, minutes_diff, MINUTE);
TIME_DIFF_FUNCTION_IMPL(SecondsDiffImpl, seconds_diff, SECOND);

template <typename FromType, typename ToType, typename Transform>
struct DateTimeOp {
    // use for (DateTime, DateTime) -> other_type
    static void vector_vector(const PaddedPODArray<FromType>& vec_from0,
                              const PaddedPODArray<FromType>& vec_from1,
                              PaddedPODArray<ToType>& vec_to) {
        size_t size = vec_from0.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i)
            vec_to[i] = Transform::execute(vec_from0[i], vec_from1[i]);
    }

    // use for (DateTime, const DateTime) -> other_type
    static void vector_constant(const PaddedPODArray<FromType>& vec_from,
                                PaddedPODArray<ToType>& vec_to, Int128& delta) {
        size_t size = vec_from.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i) vec_to[i] = Transform::execute(vec_from[i], delta);
    }

    // use for (DateTime, const ColumnNumber) -> other_type
    static void vector_constant(const PaddedPODArray<FromType>& vec_from,
                                PaddedPODArray<ToType>& vec_to, Int64 delta) {
        size_t size = vec_from.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i) vec_to[i] = Transform::execute(vec_from[i], delta);
    }

    // use for (const DateTime, ColumnNumber) -> other_type
    static void constant_vector(const FromType& from, PaddedPODArray<ToType>& vec_to,
                                const IColumn& delta) {
        size_t size = delta.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i) vec_to[i] = Transform::execute(from, delta.getInt(i));
    }

    // use for (const DateTime, DateTime) -> other_type
    static void constant_vector(const FromType& from, PaddedPODArray<ToType>& vec_to,
                                const PaddedPODArray<Int128>& delta) {
        size_t size = delta.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i) vec_to[i] = Transform::execute(from, delta[i]);
    }
};

template <typename FromType, typename Transform>
struct DateTimeAddIntervalImpl {
    static void execute(Block& block, const ColumnNumbers& arguments, size_t result) {
        using ToType = typename Transform::ReturnType::FieldType;
        using Op = DateTimeOp<FromType, ToType, Transform>;

        //        const DateLUTImpl & time_zone = extractTimeZoneFromFunctionArguments(block, arguments, 2, 0);

        const ColumnPtr source_col = block.getByPosition(arguments[0]).column;

        if (const auto* sources = checkAndGetColumn<ColumnVector<FromType>>(source_col.get())) {
            auto col_to = ColumnVector<ToType>::create();

            const IColumn& delta_column = *block.getByPosition(arguments[1]).column;

            if (const auto* delta_const_column = typeid_cast<const ColumnConst*>(&delta_column)) {
                if (delta_const_column->getField().getType() == Field::Types::Int128) {
                    Op::vector_constant(sources->getData(), col_to->getData(),
                                        delta_const_column->getField().get<Int128>());
                } else {
                    Op::vector_constant(sources->getData(), col_to->getData(),
                                        delta_const_column->getField().get<Int64>());
                }
            } else {
                const auto* delta_vec_column =
                        checkAndGetColumn<ColumnVector<FromType>>(delta_column);
                Op::vector_vector(sources->getData(), delta_vec_column->getData(),
                                  col_to->getData());
            }
            block.getByPosition(result).column = std::move(col_to);
        } else if (const auto* sources_const =
                           checkAndGetColumnConst<ColumnVector<FromType>>(source_col.get())) {
            auto col_to = ColumnVector<ToType>::create();
            if (const auto* delta_vec_column = checkAndGetColumn<ColumnVector<FromType>>(
                        *block.getByPosition(arguments[1]).column)) {
                Op::constant_vector(sources_const->template getValue<FromType>(), col_to->getData(),
                                    delta_vec_column->getData());
            } else {
                Op::constant_vector(sources_const->template getValue<FromType>(), col_to->getData(),
                                    *block.getByPosition(arguments[1]).column);
            }
            block.getByPosition(result).column = std::move(col_to);
        } else {
            throw Exception("Illegal column " +
                                    block.getByPosition(arguments[0]).column->getName() +
                                    " of first argument of function " + Transform::name,
                            ErrorCodes::ILLEGAL_COLUMN);
        }
    }
};

template <typename Transform>
class FunctionDateOrDateTimeComputation : public IFunction {
public:
    static constexpr auto name = Transform::name;
    //    static FunctionPtr create(const Context &) { return std::make_shared<FunctionDateOrDateTimeComputation>(); }
    static FunctionPtr create() { return std::make_shared<FunctionDateOrDateTimeComputation>(); }

    String getName() const override { return name; }

    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }

    DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName& arguments) const override {
        if (arguments.size() != 2 && arguments.size() != 3)
            throw Exception("Number of arguments for function " + getName() +
                                    " doesn't match: passed " + std::to_string(arguments.size()) +
                                    ", should be 2 or 3",
                            ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

        //        if (!isNativeNumber(arguments[1].type))
        //            throw Exception("Second argument for function " + getName() + " (delta) must be number",
        //                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        if (arguments.size() == 2) {
            if (!isDateOrDateTime(arguments[0].type))
                throw Exception{"Illegal type " + arguments[0].type->getName() +
                                        " of argument of function " + getName() +
                                        ". Should be a date or a date with time",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT};
        } else {
            if (!WhichDataType(arguments[0].type).isDateTime() ||
                !WhichDataType(arguments[2].type).isString())
                throw Exception("Function " + getName() +
                                        " supports 2 or 3 arguments. The 1st argument "
                                        "must be of type Date or DateTime. The 2nd argument must "
                                        "be number. "
                                        "The 3rd argument (optional) must be "
                                        "a constant string with timezone name. The timezone "
                                        "argument is allowed "
                                        "only when the 1st argument has the type DateTime",
                                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
        }

        //        if (WhichDataType(arguments[0].type).isDate())
        //        {
        //            if (std::is_same_v<decltype(Transform::execute(DataTypeDate::FieldType(), 0, std::declval<DateLUTImpl>())), UInt16>)
        //                return std::make_shared<DataTypeDate>();
        //            else
        //                return std::make_shared<DataTypeDateTime>(extractTimeZoneNameFromFunctionArguments(arguments, 2, 0));
        //        }
        //        else
        //        {
        //            if (std::is_same_v<decltype(Transform::execute(DataTypeDateTime::FieldType(), 0, std::declval<DateLUTImpl>())), UInt16>)
        return std::make_shared<typename Transform::ReturnType>();
        //            else
        //                return std::make_shared<DataTypeDateTime>(extractTimeZoneNameFromFunctionArguments(arguments, 2, 0));
        //          return std::make_shared<DataTypeDateTime>();
        //        }
    }

    bool useDefaultImplementationForConstants() const override { return true; }

    Status executeImpl(Block& block, const ColumnNumbers& arguments, size_t result,
                       size_t /*input_rows_count*/) override {
        const IDataType* from_type = block.getByPosition(arguments[0]).type.get();
        WhichDataType which(from_type);

        if (which.isDate())
            DateTimeAddIntervalImpl<DataTypeDate::FieldType, Transform>::execute(block, arguments,
                                                                                 result);
        else if (which.isDateTime())
            DateTimeAddIntervalImpl<DataTypeDateTime::FieldType, Transform>::execute(
                    block, arguments, result);
        else
            throw Exception("Illegal type " + block.getByPosition(arguments[0]).type->getName() +
                                    " of argument of function " + getName(),
                            ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return Status::OK();
    }
};

} // namespace doris::vectorized