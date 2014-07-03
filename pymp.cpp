#include <cassert>
#include <functional>
#include <vector>
#include <boost/python.hpp>

#include <stdint.h>
#include <cmp.h>

namespace py = boost::python;

static bool is_array( cmp_object_t const & obj )
{
    switch( obj.type )
    {
    case CMP_TYPE_FIXARRAY:
    case CMP_TYPE_ARRAY16:
    case CMP_TYPE_ARRAY32:
        return true;
    }

    return false;
}

static bool is_map( cmp_object_t const & obj )
{
    switch( obj.type )
    {
    case CMP_TYPE_FIXMAP:
    case CMP_TYPE_MAP16:
    case CMP_TYPE_MAP32:
        return true;
    }

    return false;
}

class Unpacker
{
public:
    Unpacker( char const * bytes, size_t len )
        : _ptr( bytes )
        , _end( bytes + len )
    {
        _stack.reserve( 512 );

        cmp_init( &_cmp, this, &Unpacker::read_bytes_, nullptr );
    }

    py::object unpack()
    {
        for( ;; )
        {
            if( auto obj = next() )
            {
                obj = try_pop( obj );

                if( obj )
                {
                    return py::object( py::handle<>( obj ) );
                }
            }
        }

        return py::object();
    }

private:
    PyObject * try_pop( PyObject * elem )
    {
        while( !_stack.empty() )
        {
            auto & top = _stack.back();

            top.append( elem );

            if( !top.complete() )
            {
                return nullptr;
            }

            elem = top.obj;

            _stack.pop_back();
        }

        return elem;
    }

    PyObject * next()
    {
        auto obj = read_next();

        if( is_array( obj ) )
        {
            if( obj.as.array_size )
            {
                push_list( obj.as.array_size );

                return nullptr;
            }

            return PyList_New( 0 );
        }
        
        if( is_map( obj ) )
        {
            if( obj.as.map_size )
            {
                push_dict( obj.as.map_size );

                return nullptr;
            }

            return PyDict_New();
        }
            
        return read_simple( obj );
    }

    void push_list( uint32_t size )
    {
        _stack.emplace_back( PyList_New( size ), size, true );
    }

    void push_dict( uint32_t size )
    {
        _stack.emplace_back( PyDict_New(), size * 2, false );
    }

    cmp_object_t read_next()
    {
        cmp_object_t obj = {};
        if( !cmp_read_object( &_cmp, &obj ) )
        {
            throw std::runtime_error( std::string( "can't read object: " ) + cmp_strerror( &_cmp) );
        }

        return obj;
    }

    PyObject * read_simple( cmp_object_t const & obj )
    {
        switch( obj.type )
        {
        case CMP_TYPE_NIL:
            return py::incref( Py_None );

        case CMP_TYPE_BOOLEAN:
            return py::incref( obj.as.boolean ? Py_True : Py_False );

        case CMP_TYPE_FLOAT:
            return PyFloat_FromDouble( obj.as.flt );
        case CMP_TYPE_DOUBLE:
            return PyFloat_FromDouble( obj.as.dbl );

        case CMP_TYPE_POSITIVE_FIXNUM:
        case CMP_TYPE_UINT8:
            return PyInt_FromLong( +obj.as.u8 );
        case CMP_TYPE_UINT16:
            return PyInt_FromLong( obj.as.u16 );
        case CMP_TYPE_UINT32:
            return PyInt_FromLong( obj.as.u32 );
        case CMP_TYPE_UINT64:
            return PyLong_FromUnsignedLongLong( obj.as.u64 );

        case CMP_TYPE_NEGATIVE_FIXNUM:
        case CMP_TYPE_SINT8:
            return PyInt_FromLong( obj.as.s8 );
        case CMP_TYPE_SINT16:
            return PyInt_FromLong( obj.as.s16 );
        case CMP_TYPE_SINT32:
            return PyInt_FromLong( obj.as.s32 );
        case CMP_TYPE_SINT64:
            return PyLong_FromLongLong( obj.as.s64 );

        case CMP_TYPE_FIXSTR:
        case CMP_TYPE_STR8:
        case CMP_TYPE_STR16:
        case CMP_TYPE_STR32:
            return read_str( obj.as.str_size );

        case CMP_TYPE_FIXARRAY:
        case CMP_TYPE_ARRAY16:
        case CMP_TYPE_ARRAY32:

        case CMP_TYPE_FIXMAP:
        case CMP_TYPE_MAP16:
        case CMP_TYPE_MAP32:

        case CMP_TYPE_FIXEXT1:
        case CMP_TYPE_FIXEXT2:
        case CMP_TYPE_FIXEXT4:
        case CMP_TYPE_FIXEXT8:
        case CMP_TYPE_FIXEXT16:

        case CMP_TYPE_BIN8:
        case CMP_TYPE_BIN16:
        case CMP_TYPE_BIN32:

        case CMP_TYPE_EXT8:
        case CMP_TYPE_EXT16:
        case CMP_TYPE_EXT32:
            assert( 0 );
            break;
        }

        throw std::runtime_error( "can't read object: unknown type" );
    }

    PyObject * read_str( uint32_t size )
    {
        if( !size )
        {
            return PyBytes_FromStringAndSize( nullptr, 0 );
        }

        if( static_cast< uint32_t >( _end - _ptr ) < size )
        {
            throw std::runtime_error( "not enough data" );
        }

        auto obj = PyBytes_FromStringAndSize( _ptr, size );

        _ptr += size;

        return obj;
    }

    static bool read_bytes_( cmp_ctx_t * ctx, void * data, size_t limit )
    {
        auto self = static_cast< Unpacker * >( ctx->buf );

        assert( self && &self->_cmp == ctx );

        return self->read_bytes( data, limit );
    }

    bool read_bytes( void * data, size_t limit )
    {
        size_t max_len = _end - _ptr;

        auto len = (std::min)( limit, max_len );

        memcpy( data, _ptr, len );

        _ptr += len;

        return len == limit;
    }
    
private:
    struct Item
    {
        PyObject * obj;
        PyObject * key;
        uint32_t size;
        uint32_t index;
        bool is_list;

        Item( PyObject * obj, uint32_t size, bool is_list )
            : obj( obj )
            , size( size )
            , is_list( is_list )
            , index()
            , key()
        {}

        bool complete()
        {
            return index == size;
        }

        void append( PyObject * item )
        {
            if( append_impl( item ) )
            {
                py::throw_error_already_set();
            }

            ++index;
        }

        int append_impl( PyObject * elem )
        {
            if( is_list )
            {
                return PyList_SetItem( obj, index, elem );
            }
           
            if( index % 2 == 1 )
            {
                auto hr = PyDict_SetItem( obj, key, elem );

                py::decref( key );
                py::decref( elem );

                key = nullptr;

                return hr;
            }

            key = elem;

            return 0;
        }

    }; // Item

    std::vector< Item > _stack;
    cmp_ctx_t _cmp;

    char const * _ptr;
    char const * _end;

}; // Unpacker

static py::object unpack( py::object bytes )
{
    auto p = bytes.ptr();

    if( !p )
    {
        throw std::runtime_error( "can't unpack: no object" );
    }

    const void * data = nullptr;
    Py_ssize_t size = 0;

    if( PyObject_AsReadBuffer( p, &data, &size ) )
    {
        py::throw_error_already_set();
    }

    if( !data || !size )
    {
        throw std::runtime_error( "can't unpack: no data" );
    }

    Unpacker unpacker( reinterpret_cast< char const * >( data ), size );

    return unpacker.unpack();
}

BOOST_PYTHON_MODULE( pymp )
{
    py::def( "unpack", unpack );
}
