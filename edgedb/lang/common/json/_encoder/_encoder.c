/*
* Copyright (c) 2012 Sprymix Inc.
* All rights reserved.
*
* See LICENSE for details.
*/

#include "_encoder_buffer.h"
#include "_encoder_buffer.c"
#include "_encoder_stringify.h"
#include "_encoder_stringify.c"
#include "_encoder.h"
#include "datetime.h"


/*===========================================================================
 * declaration and export to python
 *===========================================================================*/

static PyObject * encoder_dumps   (PyObject *self, PyObject *args);
static PyObject * encoder_dumpb   (PyObject *self, PyObject *args);
static PyObject * encoder_default (PyObject *self, PyObject *args);

static PyMethodDef EncodeMethods[] = {
    {"dumps",   encoder_dumps,   METH_VARARGS, "JSON-encode a Python object to a Python string."},
    {"dumpb",   encoder_dumpb,   METH_VARARGS, "JSON-encode a Python object to a Python bytes() array."},
    {"default", encoder_default, METH_VARARGS, "Encodes an object to a dumpable object or throws a TypeError"},
    {NULL, NULL, 0, NULL}
};

PyDoc_STRVAR(encoder_doc, "A C implementation of a JSON encoder for Python objects.\n\
\n\
Completely eqivalent to the semantix.utils.json.encoder.Encoder class:\n\
 - has equivalent dumps(), dumpb() and default() methods\n\
 - natively supports the same set of Python objects (str, int, float, True, \
   False, None, list, tuple, dict, set, frozenset, collections.OrderedDict, \
   uuid.UUID, decimal.Decimal, datetime.datetime and derived classes.\n\
 - supports __sx_serialize__() method, when vailable.");


// see http://docs.python.org/release/3.2.1/extending/newtypes.html
PyTypeObject PyEncoder_Type = {
    PyObject_HEAD_INIT(NULL)
    "_encoder.Encoder",                         /* tp_name */
    sizeof(PyEncoderObject),                    /* tp_basicsize */
    0,                                          /* tp_itemsize */
    0,                                          /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    encoder_doc,                                /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    EncodeMethods                               /* tp_methods */
};

static struct PyModuleDef encodermodule = {
    PyModuleDef_HEAD_INIT,
    "_encoder",                        /* name of module */
    NULL,                              /* module documentation, may be NULL */
    -1,                                /* -1 if the module keeps state in global variables. */
    NULL,                              /* methods - no class methods */
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit__encoder(void)
{
    PyObject* mod_decimal = PyImport_ImportModule("decimal");
    PyType_Decimal = (PyTypeObject*)PyObject_GetAttrString(mod_decimal, "Decimal");
    Py_DECREF(mod_decimal);

    PyObject* mod_uuid = PyImport_ImportModule("uuid");
    PyType_UUID = (PyTypeObject*)PyObject_GetAttrString(mod_uuid, "UUID");
    Py_DECREF(mod_uuid);

    PyObject* mod_ordereddict = PyImport_ImportModule("collections");
    PyType_OrderedDict = (PyTypeObject*)PyObject_GetAttrString(mod_ordereddict, "OrderedDict");
    Py_DECREF(mod_ordereddict);

    PyDateTime_IMPORT;

    // prepare Encoder class type/blueprint
    PyEncoder_Type.tp_new = PyType_GenericNew;
    if (PyType_Ready(&PyEncoder_Type) < 0)
        return NULL;

    // create "_encoder" module
    PyObject* module = PyModule_Create(&encodermodule);
    if (module == NULL)
        return NULL;

    // add Encoder class to the _encoder module
    Py_INCREF(&PyEncoder_Type);
    PyModule_AddObject(module, "Encoder", (PyObject *)&PyEncoder_Type);

    return module;
}


/*===========================================================================
 * implemention: public methods
 *===========================================================================*/


static void encode (PyObject *obj, EncodedData * encodedData);


/*
 * JSON-encodes a python object into a Python string. All characters in the
 * output string are guaranteed to be 7-bit ASCII.
 *
 * The first argument is the object to be JSON-encoded; the second is optional
 * integer parameter specifying max allowed recursion depth (default: 100).
 *
 * Supports optional __sx_serialize__() method and calls self.default() as the
 * last resort for all objects that could not be encoded in any other way.
 * For details see encode() function.
 */
static PyObject *
encoder_dumps (PyObject *self, PyObject *args)
{
    long max_recursion_depth = 100;
    PyObject *obj;

    if (!PyArg_ParseTuple(args, "O|l", &obj, &max_recursion_depth))
        return NULL;

    EncodedData output;

    encoder_data_init(&output, self, max_recursion_depth);

    encode(obj, &output);

    if (encoder_data_has_error(&output))
    {
        encoder_data_destruct(&output);
        return NULL;
    }

    PyObject * result = PyUnicode_FromStringAndSize(output.buffer, encoder_data_get_size(&output));

    encoder_data_destruct(&output);

    return result;
}

/*
 * JSON-encodes a python object into a Python bytes() array.
 *
 * The first argument is the object to be JSON-encoded; the second is optional
 * integer parameter specifying max allowed recursion depth (default: 100).
 *
 * Supports optional __sx_serialize__() method and calls self.default() as the
 * last resort for all objects that could not be encoded in any other way.
 * For details see encode() function.
 */
static PyObject *
encoder_dumpb (PyObject *self, PyObject *args)
{
    long max_recursion_depth = 100;
    PyObject *obj;

    if (!PyArg_ParseTuple(args, "O|l", &obj, &max_recursion_depth))
        return NULL;

    EncodedData output;

    encoder_data_init(&output, self, max_recursion_depth);

    encode(obj, &output);

    if (encoder_data_has_error(&output))
    {
        encoder_data_destruct(&output);
        return NULL;
    }

    PyObject * result = PyBytes_FromStringAndSize(output.buffer, encoder_data_get_size(&output));

    encoder_data_destruct(&output);

    return result;
}

static PyObject *
encoder_default (PyObject *self, PyObject *args)
{
    PyObject *obj;

    if (!PyArg_ParseTuple(args, "O", &obj)) return NULL;

    PyErr_Format(PyExc_TypeError, "%R is not JSON serializable", obj);

    return NULL;
}


/*===========================================================================
 * implemention: internal methods
 *===========================================================================*/

static void encode_default     (PyObject * obj,  EncodedData * encodedData);
static void encode_integer     (PyObject * obj,  EncodedData * encodedData);
static void encode_float       (PyObject * obj,  EncodedData * encodedData);
static void encode_decimal     (PyObject * obj,  EncodedData * encodedData);
static void encode_string      (PyObject * obj,  EncodedData * encodedData);
static void encode_uuid        (PyObject * obj,  EncodedData * encodedData);
static void encode_date        (PyObject * obj,  EncodedData * encodedData);
static void encode_list        (PyObject * obj,  EncodedData * encodedData);
static void encode_tuple       (PyObject * obj,  EncodedData * encodedData);
static void encode_set         (PyObject * obj,  EncodedData * encodedData);
static void encode_dict        (PyObject * obj,  EncodedData * encodedData);
static void encode_ordereddict (PyObject * obj,  EncodedData * encodedData);
static void encode_true        (EncodedData * encodedData);
static void encode_false       (EncodedData * encodedData);
static void encode_none        (EncodedData * encodedData);

/*
 * JSON-encodes a python object into the given EncodedData buffer.
 *
 * The order in which various encoders are applied to the given 'obj' is as follows:
 *
 *  1) first, the exact check for some known types (strings, int/float, true/false/none,
 *  list/tuple/dict/set, OrderedDict, UUID and Decimal) is performed and if type matches
 *  the corresponding encoder is used and the result stored in EncodedData buffer.
 *
 *  2) Next, the object's __sx_serialize__() method is tried and this function is applied
 *  to the output of __sx_serialize__.
 *
 *  3) If none of the baove worked, the more generic isinstance() check is performed
 *     against the same known object types.
 *
 *  4) If there were no match self.default() method is applied to the object and in case
 *     there were no exceptions this function is applied to the result.
 */
static void encode (PyObject *obj, EncodedData * encodedData)
{
    // if we already found an error stop and do not encode anything else
    if (encoder_data_has_error(encodedData)) return;

    // first try strict checks ---------------------------------------------

    if (PyUnicode_CheckExact(obj)) return encode_string (obj, encodedData);
    if (PyLong_CheckExact   (obj)) return encode_integer(obj, encodedData);
    if (PyFloat_CheckExact  (obj)) return encode_float  (obj, encodedData);

    if (obj == Py_True)  return encode_true (encodedData);
    if (obj == Py_False) return encode_false(encodedData);
    if (obj == Py_None)  return encode_none (encodedData);

    if (PyList_CheckExact(obj))   return encode_list (obj, encodedData);
    if (PyTuple_CheckExact(obj))  return encode_tuple(obj, encodedData);
    if (PyDict_CheckExact(obj))   return encode_dict (obj, encodedData);
    if (PyAnySet_CheckExact(obj)) return encode_set  (obj, encodedData);

    if (obj->ob_type == PyType_UUID)        return encode_uuid        (obj, encodedData);
    if (obj->ob_type == PyType_Decimal)     return encode_decimal     (obj, encodedData);
    if (obj->ob_type == PyType_OrderedDict) return encode_ordereddict (obj, encodedData);

    // try __sx_serialize__ method -----------------------------------------

    PyObject* _sx_serialize_ = PyObject_GetAttrString(obj, "__sx_serialize__");

    if (_sx_serialize_ != NULL)
    {
        PyObject* obj_encoded = PyObject_CallObject(_sx_serialize_, NULL);

        encode(obj_encoded, encodedData);

        Py_DECREF(obj_encoded);
        Py_DECREF(_sx_serialize_);

        return;
    }
    else
        PyErr_Clear();

    // try isinstance() checks ---------------------------------------------

    // need to check ordereddict-derived classes before dict-derived classes
    if (PyObject_TypeCheck(obj, PyType_OrderedDict)) return encode_ordereddict (obj, encodedData);

    if (PyDict_Check(obj))   return encode_dict (obj, encodedData);
    if (PyList_Check(obj))   return encode_list (obj, encodedData);
    if (PyTuple_Check(obj))  return encode_tuple(obj, encodedData);
    if (PyAnySet_Check(obj)) return encode_set  (obj, encodedData);

    if (PyUnicode_Check(obj)) return encode_string (obj, encodedData);
    if (PyLong_Check   (obj)) return encode_integer(obj, encodedData);
    if (PyFloat_Check  (obj)) return encode_float  (obj, encodedData);

    if (PyObject_TypeCheck(obj, PyType_UUID))    return encode_uuid   (obj, encodedData);
    if (PyObject_TypeCheck(obj, PyType_Decimal)) return encode_decimal(obj, encodedData);

    if (PyDate_Check(obj)) return encode_date(obj, encodedData);

    // try self.default() method -------------------------------------------

    return encode_default(obj, encodedData);
}

/*
 * JSON-encodes a dictionary key object into the given EncodedData buffer.
 *
 * By JSON specification only strings can be keys, thus only strings are encoded and
 * a TypeError is raised for all other object types; an exception is made for UUID
 * objects since they are also encoded to strings and UUIDs are a common dictionary
 * key in the semantix framework.
 *
 * The order in which different checks/encoders are applied is the same as in the
 * encode() method; __sx_serialize__() is also supported and is supposed to return
 * an object encodable to a string. If everything else fails the default() method
 * is called.
 */
static void encode_key (PyObject *obj, EncodedData * encodedData)
{
    // if we already found an error stop and do not encode anything else
    if (encoder_data_has_error(encodedData)) return;

    // first try strict checks ---------------------------------------------

    if (PyUnicode_CheckExact(obj))   return encode_string (obj, encodedData);
    if (obj->ob_type == PyType_UUID) return encode_uuid   (obj, encodedData);

    // try __sx_serialize__ method -----------------------------------------

    PyObject* _sx_serialize_ = PyObject_GetAttrString(obj, "__sx_serialize__");

    if (_sx_serialize_ != NULL)
    {
        PyObject* obj_encoded = PyObject_CallObject(_sx_serialize_, NULL);

        encode_key(obj_encoded, encodedData);

        Py_DECREF(obj_encoded);
        Py_DECREF(_sx_serialize_);

        return;
    }
    else
        PyErr_Clear();

    // try isinstance() checks ---------------------------------------------

    if (PyUnicode_Check(obj))                 return encode_string (obj, encodedData);
    if (PyObject_TypeCheck(obj, PyType_UUID)) return encode_uuid   (obj, encodedData);

    // try self.default() method -------------------------------------------

    encode_default(obj, encodedData);

    if (PyErr_Occurred())
    {
        // re-raise TypeError exceptions to a specifically type-error-for-dict-key;
        // leave all other exceptions as is
        if (PyErr_ExceptionMatches(PyExc_TypeError))
        {
            PyErr_Clear();
            PyErr_Format(PyExc_TypeError, "%R is not a valid dictionary key", obj);
        }

        encoder_data_set_error(encodedData);
    }
}

static void encode_default (PyObject * obj, EncodedData * encodedData)
{
    // call the "default" method of 'self' and encode the resulting Python object, if not NULL

    PyObject* default_method = PyObject_GetAttrString(encodedData->self, "default");

    if (default_method != NULL)
    {
        PyObject* obj_encoded = PyObject_CallFunctionObjArgs(default_method, obj, NULL);

        if (!PyErr_Occurred()) encode(obj_encoded, encodedData);

        Py_XDECREF(obj_encoded);
        Py_DECREF(default_method);
    }

    if (PyErr_Occurred())
        encoder_data_set_error(encodedData);
}

/*==  errors  ======================================================*/

static void encoder_simple_value_error (const char * msg, EncodedData * encodedData)
{
    PyErr_SetString(PyExc_ValueError, msg);

    encoder_data_set_error(encodedData);
}

static void encoder_value_error (const char * msg, PyObject * obj, EncodedData * encodedData)
{
    PyErr_Clear();

    PyErr_Format(PyExc_ValueError, msg, obj);

    encoder_data_set_error(encodedData);
}

static void encoder_not_serializable (PyObject * obj, EncodedData * encodedData)
{
    PyErr_Clear();

    PyErr_Format(PyExc_TypeError, "%R is not JSON serializable", obj);

    encoder_data_set_error(encodedData);
}


/*==  type-specific encoders  =====================================*/


static void encode_integer (PyObject * obj, EncodedData * encodedData)
{
    long long int_val = PyLong_AsLongLong(obj);

    if (PyErr_Occurred())
        return encoder_value_error("Number out of range: %R", obj, encodedData);

    if (int_val > JAVASCRIPT_MAXINT ||
        int_val < -JAVASCRIPT_MAXINT )
        return encoder_value_error("Number out of range: %R", obj, encodedData);

    longlong_to_string(int_val, encodedData);
}

static void encode_float (PyObject * obj, EncodedData * encodedData)
{
    double double_val = PyFloat_AS_DOUBLE(obj);

    if (Py_IS_INFINITY(double_val) || Py_IS_NAN(double_val))
    {
        if (double_val > 0 || double_val < 0)
            return encoder_simple_value_error("Infinity is not supported", encodedData);
        else
            return encoder_simple_value_error("NaN is not supported", encodedData);
    }

    // note: std json uses "return PyObject_Repr(obj)"
    //       one difference is that it keeps .0 for the whole numbers such as 2.0
    //       - but it should not matter for JavaScript

    char buffer[32];
    snprintf(buffer, 32, "%.16g", double_val);

    encoder_data_append_cstr(encodedData, buffer, 32);
}

static void encode_decimal (PyObject * obj, EncodedData * encodedData)
{
    PyObject * str_repr = PyObject_Str(obj);

    if (PyErr_Occurred()) return encoder_not_serializable(obj, encodedData);

    encode_string(str_repr, encodedData);

    Py_DECREF(str_repr);
}

static void encode_uuid (PyObject * obj, EncodedData * encodedData)
{
    PyObject * str_repr = PyObject_Str(obj);

    if (PyErr_Occurred()) return encoder_not_serializable(obj, encodedData);

    encode_string(str_repr, encodedData);

    Py_DECREF(str_repr);
}

#define HASTZINFO(p) (((_PyDateTime_BaseTZInfo *)(p))->hastzinfo)
#define GET_DT_TZINFO(p) (HASTZINFO(p) ? \
                          ((PyDateTime_DateTime *)(p))->tzinfo : Py_None)
#define GET_TD_DAYS(o)          (((PyDateTime_Delta *)(o))->days)
#define GET_TD_SECONDS(o)       (((PyDateTime_Delta *)(o))->seconds)
#define GET_TD_MICROSECONDS(o)  (((PyDateTime_Delta *)(o))->microseconds)

/*
 * 'obj' is assumed to be based on datetime.datetime.
 *
 * outputs date/time in ISO format "YYYY-MM-DDTHH:MM:SS.mmmmmm+HH:MM" to EncodedData
 */
static void encode_date (PyObject * obj, EncodedData * encodedData)
{
    if (encoder_data_has_error(encodedData)) return;

    // date in ISO format is at most 32 characters long, plus need two enclosing quotes
    encoder_data_reserve_space(encodedData, 34);

    encoder_data_append_ch_nocheck(encodedData,'"');

    datevalue_to_string(PyDateTime_GET_YEAR(obj), encodedData, 4);
    encoder_data_append_ch_nocheck(encodedData,'-');
    datevalue_to_string(PyDateTime_GET_MONTH(obj), encodedData, 2);
    encoder_data_append_ch_nocheck(encodedData,'-');
    datevalue_to_string(PyDateTime_GET_DAY(obj), encodedData, 2);
    encoder_data_append_ch_nocheck(encodedData,'T');
    datevalue_to_string(PyDateTime_DATE_GET_HOUR(obj), encodedData, 2);
    encoder_data_append_ch_nocheck(encodedData,':');
    datevalue_to_string(PyDateTime_DATE_GET_MINUTE(obj), encodedData, 2);
    encoder_data_append_ch_nocheck(encodedData,':');
    datevalue_to_string(PyDateTime_DATE_GET_SECOND(obj), encodedData, 2);

    int microseconds = PyDateTime_DATE_GET_MICROSECOND(obj);
    if (microseconds != 0)
    {
        encoder_data_append_ch_nocheck(encodedData,'.');
        datevalue_to_string(microseconds, encodedData, 6);
    }

    if (HASTZINFO(obj))
    {
        PyObject* timedelta = PyObject_CallMethod(obj, "utcoffset", NULL);

        if (timedelta != NULL)
        {
            // tzinfo's timedelta can't be more than a day and is usually with a minute
            // precision, so getting only seconds and ignoring days & microseconds is ok
            int tz_total_seconds = GET_TD_SECONDS(timedelta);

            Py_DECREF(timedelta);

            int tz_hour   = tz_total_seconds/3600;
            int tz_minute = (tz_total_seconds%3600)/60;

            encoder_data_append_ch_nocheck(encodedData,'+');
            datevalue_to_string(tz_hour, encodedData, 2);
            encoder_data_append_ch_nocheck(encodedData,':');
            datevalue_to_string(tz_minute, encodedData, 2);
        }
        else
            PyErr_Clear();
    }

    encoder_data_append_ch_nocheck(encodedData,'"');
}

#ifdef Py_UNICODE_WIDE
#define CHAR_MAX_EXPANSION (2 * 6)
#else
#define CHAR_MAX_EXPANSION 6
#endif

/*
 * Converts unicode character c to ASCII escape sequence and stores it in EncodedData.
 *
 * In the worst case EncodedData.buffer must have at least CHAR_MAX_EXPANSION bytes
 * unused to store the escaped surrogate pair "\uXXXX\uXXXX"
 */
static void encode_special_char (EncodedData * encodedData, Py_UNICODE c)
{
    static char hexchars[16] = "0123456789abcdef";

    encoder_data_append_ch_nocheck(encodedData,'\\');

    switch (c) {
        case '\\': encoder_data_append_ch_nocheck(encodedData, '\\'); break;
        case '"':  encoder_data_append_ch_nocheck(encodedData, '"');  break;
        case '\b': encoder_data_append_ch_nocheck(encodedData, 'b');  break;
        case '\f': encoder_data_append_ch_nocheck(encodedData, 'f');  break;
        case '\n': encoder_data_append_ch_nocheck(encodedData, 'n');  break;
        case '\r': encoder_data_append_ch_nocheck(encodedData, 'r');  break;
        case '\t': encoder_data_append_ch_nocheck(encodedData, 't');  break;
        case '/':  encoder_data_append_ch_nocheck(encodedData, '/'); break;
        default:
            #ifdef Py_UNICODE_WIDE
            if (c >= 0x10000) {
                // UTF-16 surrogate pair
                Py_UNICODE v = c - 0x10000;
                c = 0xd800 | ((v >> 10) & 0x3ff);
                encoder_data_append_ch_nocheck(encodedData, 'u');
                encoder_data_append_ch_nocheck(encodedData, hexchars[(c >> 12) & 0xf]);
                encoder_data_append_ch_nocheck(encodedData, hexchars[(c >>  8) & 0xf]);
                encoder_data_append_ch_nocheck(encodedData, hexchars[(c >>  4) & 0xf]);
                encoder_data_append_ch_nocheck(encodedData, hexchars[(c      ) & 0xf]);
                c = 0xdc00 | (v & 0x3ff);
                encoder_data_append_ch_nocheck(encodedData, '\\');
            }
            #endif
            encoder_data_append_ch_nocheck(encodedData, 'u');
            encoder_data_append_ch_nocheck(encodedData, hexchars[(c >> 12) & 0xf]);
            encoder_data_append_ch_nocheck(encodedData, hexchars[(c >>  8) & 0xf]);
            encoder_data_append_ch_nocheck(encodedData, hexchars[(c >>  4) & 0xf]);
            encoder_data_append_ch_nocheck(encodedData, hexchars[(c      ) & 0xf]);
    }
}

static void encode_string (PyObject * pystr, EncodedData * encodedData)
{
    Py_ssize_t  input_size;
    Py_UNICODE* input_unicode;

    input_size    = PyUnicode_GET_SIZE(pystr);
    input_unicode = PyUnicode_AS_UNICODE(pystr);

    // reserve as much space as we may possibly need assuming we have to
    // unicode-expand all the symbols in this python string
    encoder_data_reserve_space(encodedData, input_size * CHAR_MAX_EXPANSION);

    encoder_data_append_ch_nocheck(encodedData,'"');

    Py_ssize_t i;
    for (i = 0; i < input_size; i++)
    {
        const Py_UNICODE c = input_unicode[i];

        if (c >= ' ' && c <= '~' && c != '"' && c != '/' && c != '\\')
        //if (c >= ' ' && c <= '~' && c != '\\' && c != '"')
            encoder_data_append_ch_nocheck(encodedData, c);
        else
            encode_special_char(encodedData, c);
    }

    encoder_data_append_ch_nocheck(encodedData,'"');
}

static void encode_true (EncodedData * encodedData)
{
    if (!encoder_data_reserve_space(encodedData, 4)) return;
    encoder_data_append_ch_nocheck(encodedData, 't');
    encoder_data_append_ch_nocheck(encodedData, 'r');
    encoder_data_append_ch_nocheck(encodedData, 'u');
    encoder_data_append_ch_nocheck(encodedData, 'e');
}

static void encode_false (EncodedData * encodedData)
{
    if (!encoder_data_reserve_space(encodedData, 5)) return;
    encoder_data_append_ch_nocheck(encodedData, 'f');
    encoder_data_append_ch_nocheck(encodedData, 'a');
    encoder_data_append_ch_nocheck(encodedData, 'l');
    encoder_data_append_ch_nocheck(encodedData, 's');
    encoder_data_append_ch_nocheck(encodedData, 'e');
}

static void encode_none (EncodedData * encodedData)
{
    if (!encoder_data_reserve_space(encodedData, 4)) return;
    encoder_data_append_ch_nocheck(encodedData, 'n');
    encoder_data_append_ch_nocheck(encodedData, 'u');
    encoder_data_append_ch_nocheck(encodedData, 'l');
    encoder_data_append_ch_nocheck(encodedData, 'l');
}

/* used for max-recursion-depth/loop tracking */
static void inc_depth (EncodedData * encodedData)
{
    encodedData->depth++;

    if (encodedData->depth > encodedData->max_depth)
    {
        PyErr_Format(PyExc_ValueError,
                     "Exceeded maximum allowed recursion level (%d), " \
                     "possibly circular reference detected", encodedData->max_depth);

        encoder_data_set_error(encodedData);
    }
}

/* used for max-recursion-depth/loop tracking */
static void dec_depth (EncodedData * encodedData)
{
    encodedData->depth--;
}

static void encode_list (PyObject * obj, EncodedData * encodedData)
{
    inc_depth(encodedData);

    Py_ssize_t list_size = PyList_GET_SIZE(obj);

    encoder_data_append_char(encodedData, '[');

    Py_ssize_t i;
    for (i = 0; i < list_size; i++)
    {
        if (i!=0) encoder_data_append_char(encodedData, ',');

        encode(PyList_GET_ITEM(obj, i), encodedData);
    }

    encoder_data_append_char(encodedData, ']');

    dec_depth(encodedData);
}

static void encode_tuple (PyObject * obj, EncodedData * encodedData)
{
    inc_depth(encodedData);

    Py_ssize_t tuple_size = PyTuple_Size(obj);

    encoder_data_append_char(encodedData, '[');

    Py_ssize_t i;
    for (i = 0; i < tuple_size; i++)
    {
        if (i!=0) encoder_data_append_char(encodedData, ',');

        encode(PyTuple_GET_ITEM(obj, i), encodedData);
    }

    encoder_data_append_char(encodedData, ']');

    dec_depth(encodedData);
}

static void encode_dict (PyObject * obj, EncodedData * encodedData)
{
    inc_depth(encodedData);

    encoder_data_append_char(encodedData, '{');

    bool has_values = false;
    Py_ssize_t pos;
    PyObject *key, *value;

    pos = 0;
    while (PyDict_Next(obj, &pos, &key, &value))
    {
        if (has_values) encoder_data_append_char(encodedData, ',');
        has_values = true;

        encode_key (key, encodedData);

        if (encoder_data_has_error(encodedData)) return;

        encoder_data_append_char(encodedData, ':');

        encode (value, encodedData);

        if (encoder_data_has_error(encodedData)) return;
    }

    encoder_data_append_char(encodedData, '}');

    dec_depth(encodedData);
}

static void encode_set (PyObject * obj, EncodedData * encodedData)
{
    inc_depth(encodedData);

    Py_ssize_t set_size = PyObject_Size(obj);

    if (set_size == -1) return encoder_not_serializable(obj, encodedData);

    PyObject * it = PyObject_GetIter(obj);

    if (it == NULL) return encoder_not_serializable(obj, encodedData);

    encoder_data_append_char(encodedData, '[');

    bool has_values = false;
    PyObject *value;
    while ((value = PyIter_Next(it)) != NULL)
    {
        if (has_values) encoder_data_append_char(encodedData, ',');
        has_values = true;

        encode(value, encodedData);

        Py_DECREF(value);
    }
    Py_DECREF(it);

    encoder_data_append_char(encodedData, ']');

    dec_depth(encodedData);

    if (PyErr_Occurred()) return encoder_not_serializable(obj, encodedData);
}

static void encode_ordereddict (PyObject * obj, EncodedData * encodedData)
{
    inc_depth(encodedData);

    PyObject * it = PyObject_GetIter(obj);

    if (it == NULL) return encoder_not_serializable(obj, encodedData);

    encoder_data_append_char(encodedData, '{');

    bool has_values = false;
    PyObject *value;
    PyObject *key;
    while ((key = PyIter_Next(it)) != NULL)
    {
        if (has_values) encoder_data_append_char(encodedData, ',');
        has_values = true;

        encode_key(key, encodedData);

        if (encoder_data_has_error(encodedData)) return;

        encoder_data_append_char(encodedData, ':');

        value = PyDict_GetItem(obj, key);

        encode(value, encodedData);

        if (encoder_data_has_error(encodedData)) return;

        Py_DECREF(key);
    }
    Py_DECREF(it);

    encoder_data_append_char(encodedData, '}');

    dec_depth(encodedData);
}
