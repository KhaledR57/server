/*
   Copyright (c) 2023-2025, MariaDB Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#define MYSQL_SERVER
#include "my_global.h"
#include "sql_class.h"          // THD
#include "sp_head.h"
#include "mysql/plugin_data_type.h"
#include "mysql/plugin_function.h"
#include "sp_instr.h"
#include "sql_type.h"


class Type_collection_cursor: public Type_collection
{
protected:
  const Type_handler *aggregate_common(const Type_handler *h1,
                                       const Type_handler *h2) const;
public:
  const Type_handler *aggregate_for_result(const Type_handler *h1,
                                           const Type_handler *h2)
                                           const override
  {
    return aggregate_common(h1, h2);
  }
  const Type_handler *aggregate_for_comparison(const Type_handler *h1,
                                               const Type_handler *h2)
                                               const override
  {
    DBUG_ASSERT(h1 == h1->type_handler_for_comparison());
    DBUG_ASSERT(h2 == h2->type_handler_for_comparison());
    return nullptr;
  }
  const Type_handler *aggregate_for_min_max(const Type_handler *h1,
                                            const Type_handler *h2)
                                            const override
  {
    return nullptr;
  }
  const Type_handler *aggregate_for_num_op(const Type_handler *h1,
                                           const Type_handler *h2)
                                           const override
  {
    return nullptr;
  }
};


static Type_collection_cursor type_collection_cursor;


class Field_sys_refcursor :public Field_short
{
  void ref_count_inc(THD *thd, longlong offset)
  {
    if (offset >= 0 &&
        offset < (longlong) thd->m_session_cursors.elements())
      thd->m_session_cursors.at((size_t) offset).ref_count_inc();
  }

  void ref_count_dec(THD *thd, longlong offset)
  {
    if (offset >= 0 &&
        offset < (longlong) thd->m_session_cursors.elements())
      thd->m_session_cursors.at((size_t) offset).ref_count_dec();
  }

  void after_update(THD *thd, const Longlong_null &old_value,
                              const Longlong_null &new_value)
  {
    if (old_value.is_null())
    {
      if (!new_value.is_null())
        ref_count_inc(thd, new_value.value());
    }
    else if (new_value.is_null())
    {
      ref_count_dec(thd, old_value.value());
    }
    else if (old_value.value() != new_value.value())
    {
      ref_count_dec(thd, old_value.value());
      ref_count_inc(thd, new_value.value());
    }
  }

public:
  Field_sys_refcursor(const LEX_CSTRING &name, const Record_addr &addr,
                      enum utype unireg_check_arg, uint32 len_arg)
    :Field_short(addr.ptr(), len_arg, addr.null_ptr(), addr.null_bit(),
                 Field::NONE, &name, false/*zerofill*/, true/*unsigned*/)
  {}
  void sql_type(String &res) const override
  {
    res.set_ascii(STRING_WITH_LEN("sys_refcursor"));
  }
  const Type_handler *type_handler() const override;
  bool memcpy_field_possible(const Field *from) const override
  {
    return false;
  }

  bool sp_prepare_and_store_item(THD *thd, Item **value) override
  {
    DBUG_ENTER("Field_sys_refcursor::sp_prepare_and_store_item");
    DBUG_ASSERT(value);
    Item *expr_item;
    const Longlong_null old_value= to_longlong_null();

    if (!(expr_item= thd->sp_fix_func_item_for_assignment(this, value)) ||
        expr_item->check_is_evaluable_expression_or_error() ||
        (expr_item->save_in_field(this, 0), thd->is_error()))
    {
      set_null();
      after_update(thd, old_value, Longlong_null());
      DBUG_ASSERT(thd->is_error());
      DBUG_RETURN(true);
    }
    after_update(thd, old_value, to_longlong_null());
    DBUG_RETURN(false);
  }

  bool sp_variable_destruct(THD *thd) override
  {
    sp_cursor_array_element *cursor= thd->m_session_cursors.
                                       get_cursor_by_ref(thd, this, false);
    if (!cursor)
    {
      set_null();
      return false; // An uninitialized SYS_REFCURSOR variable
    }
    DBUG_ASSERT(!is_null());
    cursor->ref_count_dec();
    if (!cursor->ref_count() && cursor->is_open())
      cursor->close(thd);
    set_null();
    return false;
  }

};


class Type_handler_sys_refcursor: public Type_handler_ushort
{
public:
  const Type_collection *type_collection() const override
  {
    return &type_collection_cursor;
  }
  const Type_handler *type_handler_for_comparison() const override
  {
    return this;
  }
  const Type_handler *type_handler_signed() const override
  {
    return this;
  }
  const Type_handler *type_handler_unsigned() const override
  {
    return this;
  }

  bool can_return_bool() const override { return false; }
  bool can_return_int() const override { return false; }
  bool can_return_decimal() const override { return false; }
  bool can_return_real() const override { return false; }
  bool can_return_str() const override { return false; }
  bool can_return_text() const override { return false; }
  bool can_return_date() const override { return false; }
  bool can_return_time() const override { return false; }
  bool can_return_extract_source(interval_type type) const override
  {
    return false;
  }

  bool Item_sum_sum_fix_length_and_dec(Item_sum_sum *item) const override
  {
    static const LEX_CSTRING name= {STRING_WITH_LEN("sum") };
    return Item_func_or_sum_illegal_param(name);
  }

  bool Item_sum_avg_fix_length_and_dec(Item_sum_avg *item) const override
  {
    static const LEX_CSTRING name= {STRING_WITH_LEN("avg") };
    return Item_func_or_sum_illegal_param(name);
  }

  bool Item_sum_variance_fix_length_and_dec(Item_sum_variance *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_func_round_fix_length_and_dec(Item_func_round *item) const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_func_int_val_fix_length_and_dec(Item_func_int_val *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_func_abs_fix_length_and_dec(Item_func_abs *item) const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_func_neg_fix_length_and_dec(Item_func_neg *item) const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_func_signed_fix_length_and_dec(Item_func_signed *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_func_unsigned_fix_length_and_dec(Item_func_unsigned *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_double_typecast_fix_length_and_dec(Item_double_typecast *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_float_typecast_fix_length_and_dec(Item_float_typecast *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_decimal_typecast_fix_length_and_dec(Item_decimal_typecast *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_char_typecast_fix_length_and_dec(Item_char_typecast *item)
                                                                const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_time_typecast_fix_length_and_dec(Item_time_typecast *item)
                                                                 const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_date_typecast_fix_length_and_dec(Item_date_typecast *item)
                                                                 const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  bool Item_datetime_typecast_fix_length_and_dec(Item_datetime_typecast *item)
                                                                 const override
  {
    return Item_func_or_sum_illegal_param(item);
  }

  Item * create_typecast_item(THD *thd, Item *item,
                              const Type_cast_attributes &attr) const override
  {
    return NULL;
  }
  /*
    Create a Field as a storage for an SP variable.
    Note, creating a field for a real table is prevented in the methods below:
    - make_table_field()
    - Column_definition_set_attributes()
  */
  Field *make_table_field_from_def(TABLE_SHARE *share, MEM_ROOT *root,
                                   const LEX_CSTRING *name,
                                   const Record_addr &rec, const Bit_addr &bit,
                                   const Column_definition_attributes *attr,
                                   uint32 flags) const override
  {
    return new (root) Field_sys_refcursor(*name, rec, attr->unireg_check,
                                          (uint32) attr->length);
  }

  // Disallow "CREATE TABLE t1 AS SELECT sys_refcursor_var;"
  Field *make_table_field(MEM_ROOT *root,
                          const LEX_CSTRING *name,
                          const Record_addr &addr,
                          const Type_all_attributes &attr,
                          TABLE_SHARE *share) const override
  {
    my_error(ER_ILLEGAL_PARAMETER_DATA_TYPE_FOR_OPERATION, MYF(0),
             "SYS_REFCURSOR", "CREATE TABLE");
    return nullptr;
  }

  // Disallow "CREATE TABLE t1 (a SYS_REFCURSOR)"
  bool Column_definition_set_attributes(THD *thd,
                                        Column_definition *def,
                                        const Lex_field_type_st &attr,
                                        column_definition_type_t type)
                                        const override
  {
    if (type == COLUMN_DEFINITION_TABLE_FIELD)
    {
      my_error(ER_ILLEGAL_PARAMETER_DATA_TYPE_FOR_OPERATION, MYF(0),
               "SYS_REFCURSOR", "CREATE TABLE");
      return true;
    }
    /*
      Oracle returns an error on SYS_REFCURSOR variable declarations
      in the top level frame of a package or a package body,
      i.e. in the frame following immediately after IS/AS.
      For example, this script:
        CREATE PACKAGE BODY pkg AS
          cur SYS_REFCURSOR;
          ... functions and procedures ...
        END;
      returns an error:
        "Cursor Variables cannot be declared as part of a package"
      SYS_REFCURSOR can only appear in a package as:
      - a package routine parameter
      - a package function return value
      - in the package body initialization section
      Let's return an error on the top level, like Oracle does.
      The sp_pcontext frame layout is as follows:
      - The the top level sp_pcontext stands for routine parameters.
        In case of a package it still exists but contains no variables.
        It's parent_context() is nullptr.
      - The second sp_pcontext contains declarations in the AS/IS section.
      Let's check parent_context()->parent_context() and raise an
      error if we get nullptr.
    */
    if (type == COLUMN_DEFINITION_ROUTINE_LOCAL &&
        thd->lex->sphead && thd->lex->sphead->get_package() &&
        thd->lex->spcont->parent_context() &&
        !thd->lex->spcont->parent_context()->parent_context())
    {
      my_error(ER_NOT_ALLOWED_IN_THIS_CONTEXT, MYF(0), "SYS_REFCURSOR");
      return true;
    }
    return Type_handler_ushort::Column_definition_set_attributes(thd, def,
                                                                 attr, type);
  }

  bool has_sp_variable_destructor() const override
  {
    return true;
  }

};


static Type_handler_sys_refcursor type_handler_sys_refcursor;


const Type_handler *Field_sys_refcursor::type_handler() const
{
  return &type_handler_sys_refcursor;
}


const Type_handler *
Type_collection_cursor::aggregate_common(const Type_handler *h1,
                                         const Type_handler *h2) const
{
  if (h1 == h2)
   return h1;

  static const Type_aggregator::Pair agg[]=
  {
    {
      &type_handler_sys_refcursor,
      &type_handler_null,
      &type_handler_sys_refcursor
    },
    {NULL,NULL,NULL}
  };
  return Type_aggregator::find_handler_in_array(agg, h1, h2, true);
}


static struct st_mariadb_data_type plugin_descriptor_type_sys_refcursor=
{
  MariaDB_DATA_TYPE_INTERFACE_VERSION,
  &type_handler_sys_refcursor
};


/*************************************************************************/

#ifndef DBUG_OFF
class Item_func_cursor_ref_count :public Item_long_func
{
public:
  Item_func_cursor_ref_count(THD *thd, Item *pos)
   :Item_long_func(thd, pos)
  { }
  LEX_CSTRING func_name_cstring() const override
  {
    static LEX_CSTRING name= {STRING_WITH_LEN("cursor_ref_count") };
    return name;
  }
  bool const_item() const override
  {
    return false;
  }
  bool check_vcol_func_processor(void *arg) override
  {
    return mark_unsupported_function(func_name(), "()", arg, VCOL_SESSION_FUNC);
  }
  bool fix_length_and_dec(THD *thd) override
  {
    bool rc= Item_long_func::fix_length_and_dec(thd);
    set_maybe_null(true);
    return rc;
  }
  longlong val_int() override
  {
    THD *thd= current_thd;
    sp_cursor_array *array= &thd->m_session_cursors;
    longlong value= args[0]->val_int();
    if ((null_value= args[0]->null_value || value < 0 || value >= (longlong) array->elements()))
      return 0;
    return thd->m_session_cursors.at(value).ref_count();
  }
  Item *do_get_copy(THD *thd) const override
  { return get_item_copy<Item_func_cursor_ref_count>(thd, this); }
};


class Create_func_cursor_ref_count :public Create_func_arg1
{
public:
  Item *create_1_arg(THD *thd, Item *pos) override;
  static Create_func_cursor_ref_count s_singleton;
protected:
  Create_func_cursor_ref_count() {}
};


Create_func_cursor_ref_count Create_func_cursor_ref_count::s_singleton;

Item* Create_func_cursor_ref_count::create_1_arg(THD *thd, Item *pos)
{
  return new (thd->mem_root) Item_func_cursor_ref_count(thd, pos);
}

#define BUILDER(F) & F::s_singleton

static Plugin_function
  plugin_descriptor_function_cursor_ref_count(BUILDER(Create_func_cursor_ref_count));

#endif

/*************************************************************************/

maria_declare_plugin(type_cursor)
{
  MariaDB_DATA_TYPE_PLUGIN,     // the plugin type (see include/mysql/plugin.h)
  &plugin_descriptor_type_sys_refcursor, // a pointer to the plugin descriptor
  "sys_refcursor",              // plugin name
  "MariaDB Corporation",        // plugin author
  "Data type SYS_REFCURSOR",    // the plugin description
  PLUGIN_LICENSE_GPL,           // the plugin license (see include/mysql/plugin.h)
  0,                            // Pointer to plugin initialization function
  0,                            // Pointer to plugin deinitialization function
  0x0100,                       // Numeric version 0xAABB means AA.BB version
  NULL,                         // Status variables
  NULL,                         // System variables
  "1.0",                        // String version representation
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL // Maturity(see include/mysql/plugin.h)*/
}
#ifndef DBUG_OFF
,
{
  MariaDB_FUNCTION_PLUGIN,      // the plugin type (see include/mysql/plugin.h)
  &plugin_descriptor_function_cursor_ref_count, // ptr to the plugin descriptor
  "cursor_ref_count",           // plugin name
  "MariaDB Corporation",        // plugin author
  "Function CURSOR_REF_COUNT()",// the plugin description
  PLUGIN_LICENSE_GPL,           // the plugin license (see include/mysql/plugin.h)
  0,                            // Pointer to plugin initialization function
  0,                            // Pointer to plugin deinitialization function
  0x0100,                       // Numeric version 0xAABB means AA.BB version
  NULL,                         // Status variables
  NULL,                         // System variables
  "1.0",                        // String version representation
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL // Maturity(see include/mysql/plugin.h)*/
}
#endif
maria_declare_plugin_end;
