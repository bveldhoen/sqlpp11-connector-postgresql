/**
 * Copyright © 2014-2015, Matthijs Möhlmann
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SQLPP_POSTGRESQL_RETURNING_COLUMN_LIST_H
#define SQLPP_POSTGRESQL_RETURNING_COLUMN_LIST_H

#include <tuple>
#include <sqlpp11/operand_check.h>
#include <sqlpp11/result_row.h>
#include <sqlpp11/table.h>
#include <sqlpp11/data_types/no_value.h>
#include <sqlpp11/field_spec.h>
#include <sqlpp11/expression_fwd.h>
#include <sqlpp11/select_pseudo_table.h>
#include <sqlpp11/named_interpretable.h>
#include <sqlpp11/interpret_tuple.h>
#include <sqlpp11/policy_update.h>
#include <sqlpp11/detail/type_set.h>
#include <sqlpp11/detail/copy_tuple_args.h>

namespace sqlpp
{
  SQLPP_VALUE_TRAIT_GENERATOR(is_returning_column_list)

  namespace postgresql
  {
    namespace detail
    {
      template <typename... Columns>
      struct returning_traits
      {
        using _traits = make_traits<no_value_t, tag::is_returning_column_list, tag::is_return_value>;
        struct _alias_t
        {
        };
      };

      template <typename Column>
      struct returning_traits<Column>
      {
        using _traits = make_traits<value_type_of<Column>,
                                    tag::is_returning_column_list,
                                    tag::is_return_value,
                                    tag::is_expression,
                                    tag::is_selectable>;  // TODO: Is this correct?
        using _alias_t = typename Column::_alias_t;
      };
    }

    template <typename Db>
    struct dynamic_returning_column_list
    {
      using _names_t = std::vector<std::string>;
      std::vector<named_interpretable_t<Db>> _dynamic_columns;
      _names_t _dynamic_expression_names;

      template <typename Expr>
      void emplace_back(Expr expr)
      {
        _dynamic_expression_names.push_back(name_of<Expr>::char_ptr());
        _dynamic_columns.emplace_back(expr);
      }

      bool empty() const
      {
        return _dynamic_columns.empty();
      }
    };

    template <>
    struct dynamic_returning_column_list<void>
    {
      struct _names_t
      {
        static constexpr size_t size()
        {
          return 0;
        }
      };
      _names_t _dynamic_expression_names;

      static constexpr bool empty()
      {
        return true;
      }
    };
  }

  template <typename Context, typename Db>
  struct serializer_t<Context, postgresql::dynamic_returning_column_list<Db>>
  {
    using T = postgresql::dynamic_returning_column_list<Db>;

    static Context& _(const T& t, Context& context)
    {
      bool first{true};
      for (const auto column : t._dynamic_columns)
      {
        if (first)
        {
          first = false;
        }
        else
        {
          context << ',';
        }

        serialize(column, context);
      }

      return context;
    }
  };

  template <typename Context>
  struct serializer_t<Context, postgresql::dynamic_returning_column_list<void>>
  {
    using T = postgresql::dynamic_returning_column_list<void>;

    static Context& _(const T&, Context& context)
    {
      return context;
    }
  };

  namespace postgresql
  {
    template <typename Database, typename... Columns>
    struct returning_column_list_data_t
    {
      returning_column_list_data_t(Columns... columns) : _columns(columns...)
      {
      }
      returning_column_list_data_t(std::tuple<Columns...> columns) : _columns(columns)
      {
      }
      returning_column_list_data_t(const returning_column_list_data_t&) = default;
      returning_column_list_data_t(returning_column_list_data_t&&) = default;
      returning_column_list_data_t& operator=(const returning_column_list_data_t&) = default;
      returning_column_list_data_t& operator=(returning_column_list_data_t&&) = default;

      std::tuple<Columns...> _columns;
      dynamic_returning_column_list<Database> _dynamic_columns;
    };

    // static asserts...
    SQLPP_PORTABLE_STATIC_ASSERT(
        assert_no_unknown_tables_in_returning_columns_t,
        "at least one returning column requires a table which is otherwise not known in the statement");

    // Columns in returning list
    template <typename Database, typename... Columns>
    struct returning_column_list_t
    {
      using _traits = typename detail::returning_traits<Columns...>::_traits;
      using _nodes = ::sqlpp::detail::type_vector<Columns...>;
      using _alias_t = typename detail::returning_traits<Columns...>::_alias_t;
      using _is_dynamic = is_database<Database>;

      struct _column_type
      {
      };

      // Data
      using _data_t = returning_column_list_data_t<Database, Columns...>;

      // TODO: Implementation
      template <typename Policies>
      struct _impl_t
      {
        // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2173269
        _impl_t() = default;
        _impl_t(const _data_t& data) : _data(data)
        {
        }

        template <typename NamedExpression>
        void add_ntc(NamedExpression namedExpression)
        {
          add<NamedExpression, std::false_type>(namedExpression);
        }

        template <typename NamedExpression, typename TableCheckRequired = std::true_type>
        void add(NamedExpression namedExpression)
        {
          using named_expression = auto_alias_t<NamedExpression>;
          // Asserts...
          using column_names = ::sqlpp::detail::make_type_set_t<typename Columns::_alias_t...>;
          // More asserts...
          using _serialize_check = sqlpp::serialize_check_t<typename Database::_serializer_context_t, named_expression>;
          _serialize_check::_();

          using ok =
              logic::all_t<_is_dynamic::value, is_selectable_t<named_expression>::value, _serialize_check::type::value>;

          _add_impl(namedExpression, ok());
        }

        template <typename NamedExpression>
        void _add_impl(NamedExpression namedExpression, const std::true_type&)
        {
          return _data._dynamic_columns.emplace_back(auto_alias_t<NamedExpression>{namedExpression});
        }

        template <typename NamedExpression>
        void _add_impl(NamedExpression namedExpression, const std::false_type&);

        _data_t _data;
      };

      // TODO: Base template to be inherited by statement
      template <typename Policies>
      struct _base_t
      {
        using _data_t = returning_column_list_data_t<Database, Columns...>;

        // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2173269
        template <typename... Args>
        _base_t(Args&&... args)
            : returning_columns{std::forward<Args>(args)...}
        {
        }

        _impl_t<Policies> returning_columns;
        _impl_t<Policies>& operator()()
        {
          return returning_columns;
        }
        const _impl_t<Policies>& operator()() const
        {
          return returning_columns;
        }

        _impl_t<Policies>& get_selected_columns()
        {
          return returning_columns;
        }
        const _impl_t<Policies>& get_selected_columns() const
        {
          return returning_columns;
        }

        template <typename T>
        static auto _get_member(T t) -> decltype(t.returning_columms)
        {
          return t.returning_columns;
        }

        // Checks
        using _column_check =
            typename std::conditional<Policies::template _no_unknown_tables<returning_column_list_t>::value,
                                      consistent_t,
                                      assert_no_unknown_tables_in_returning_columns_t>::type;

        // Returning and aggregate??
        // using _aggregate_check = ...

        using _consistency_check = ::sqlpp::detail::get_first_if<is_inconsistent_t, consistent_t, _column_check>;
      };

      // Result methods
      template <typename Statement>
      struct _result_methods_t
      {
        using _statement_t = Statement;

        const _statement_t& _get_statement() const
        {
          return static_cast<const _statement_t&>(*this);
        }

        template <typename Db, typename Column>
        struct _deferred_field_t
        {
          using type = make_field_spec_t<_statement_t, Column>;
        };

        template <typename Db, typename Column>
        using _field_t = typename _deferred_field_t<Db, Column>::type;

        template <typename Db>
        using _result_row_t = typename std::conditional<_is_dynamic::value,
                                                        dynamic_result_row_t<Db, _field_t<Db, Columns>...>,
                                                        result_row_t<Db, _field_t<Db, Columns>...>>::type;

        using _dynamic_names_t = typename dynamic_returning_column_list<Database>::_names_t;

        template <typename AliasProvider>
        struct _deferred_table_t
        {
          using table = select_pseudo_table_t<_statement_t, Columns...>;
          using alias = typename table::template _alias_t<AliasProvider>;
        };

        template <typename AliasProvider>
        using _table_t = typename _deferred_table_t<AliasProvider>::table;

        template <typename AliasProvider>
        using _alias_t = typename _deferred_table_t<AliasProvider>::alias;

        template <typename AliasProvider>
        _alias_t<AliasProvider> as(const AliasProvider& aliasProvider) const
        {
          consistency_check_t<_statement_t>::_();
          // static asserts...
          return _table_t<AliasProvider>(_get_statement()).as(aliasProvider);
        }

        const _dynamic_names_t& get_dynamic_names() const
        {
          return _get_statement().get_selected_columns()._data._dynamic_columns._dynamic_expression_names;
        }

        size_t get_no_of_result_columns() const
        {
          return sizeof...(Columns) + get_dynamic_names().size();
        }

        // auto ..
        template <typename Db, typename Composite>
        auto _run(Db& db, const Composite& composite) const
            -> result_t<decltype(db.select(composite)), _result_row_t<Db>>
        {
          return {db.select(composite), get_dynamic_names()};
        }

        template <typename Db>
        auto _run(Db& db) const -> result_t<decltype(db.select(std::declval<_statement_t>())), _result_row_t<Db>>
        {
          return {db.select(_get_statement()), get_dynamic_names()};
        }

        // Prepare
        template <typename Db, typename Composite>
        auto _prepare(Db& db, const Composite& composite) const -> prepared_select_t<Db, _statement_t, Composite>
        {
          return {make_parameter_list_t<Composite>{}, get_dynamic_names(), db.prepare_select(composite)};
        }

        template <typename Db>
        auto _prepare(Db& db) const -> prepared_select_t<Db, _statement_t>
        {
          return {make_parameter_list_t<_statement_t>{}, get_dynamic_names(), db.prepare_select(_get_statement())};
        }
      };
    };

    namespace detail
    {
      template <typename Database, typename... Columns>
      using make_returning_column_list_t =
          ::sqlpp::detail::copy_tuple_args_t<returning_column_list_t,
                                             Database,
                                             decltype(column_tuple_merge(std::declval<Columns>()...))>;
    }

    struct no_returning_column_list_t
    {
      using _traits = make_traits<no_value_t, tag::is_noop, tag::is_missing>;
      using _nodes = ::sqlpp::detail::type_vector<>;

      struct _alias_t
      {
      };

      // Data
      using _data_t = no_data_t;

      // Member implementation with data and methods
      template <typename Policies>
      struct _impl_t
      {
        // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2173269
        _impl_t() = default;
        _impl_t(const _data_t& data) : _data(data)
        {
        }

        _data_t _data;
      };

      // TODO: Base template to be inherited
      template <typename Policies>
      struct _base_t
      {
        using _data_t = no_data_t;

        // workaround for msvc bug https://connect.microsoft.com/VisualStudio/Feedback/Details/2173269
        template <typename... Args>
        _base_t(Args&... args)
            : no_returned_columns{std::forward<Args>(args)...}
        {
        }

        _impl_t<Policies> no_returned_columns;
        _impl_t<Policies>& operator()()
        {
          return no_returned_columns;
        }
        const _impl_t<Policies>& operator()() const
        {
          return no_returned_columns;
        }

        using _database_t = typename Policies::_database_t;

        template <typename Check, typename T>
        using _new_statement_t = new_statement_t<Check::value, Policies, no_returning_column_list_t, T>;
        using _consistency_check = consistent_t;

        template <typename... Args>
        auto columns(Args... args) const
            -> _new_statement_t<decltype(_check_args(args...)), detail::make_returning_column_list_t<void, Args...>>
        {
        }
      };
    };
  }

  // Interpreters
  template <typename Context, typename Database, typename... Columns>
  struct serializer_t<Context, postgresql::returning_column_list_data_t<Database, Columns...>>
  {
    using _serialize_check = serialize_check_of<Context, Columns...>;
    using T = postgresql::returning_column_list_data_t<Database, Columns...>;

    static Context& _(const T& t, Context& context)
    {
      context << " RETURNING ";
      interpret_tuple(t._columns, ',', context);
      if (sizeof...(Columns) and not t._dynamic_columns.empty())
        context << ',';
      serialize(t._dynamic_columns, context);
      return context;
    }
  };

  namespace postgresql
  {
    template <typename... T>
    auto returning_columns(T&&... t)
        -> decltype(statement_t<void, no_returning_column_list_t>().columns(std::forward<T>(t)...))
    {
      return statement_t<void, no_returning_column_list_t>().columns(std::forward<T>(t)...);
    }
  }
}

#endif
