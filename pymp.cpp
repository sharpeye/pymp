#include <cassert>
#include <string>
#include <functional>
#include <stack>
#include <boost/python.hpp>

#include <stdint.h>
#include <cmp.h>

namespace py = boost::python;

class Reader
{
public:
    Reader( char const * bytes, size_t len )
        : _ptr( bytes )
        , _end( bytes + len )
    {
        cmp_init( &_cmp, this, &Reader::read_bytes_, nullptr );
    }

    py::object operator() ()
    {
        py::object hr;

        _stack.emplace( [&]{
            hr = read_obj();
        } );

        while( !_stack.empty() )
        {
            auto f = std::move( _stack.top() );
            _stack.pop();

            f();
        }

        return hr;
    }

private:
    py::object read_obj()
    {
        cmp_object_t obj = {};
        if( !cmp_read_object( &_cmp, &obj ) )
        {
            throw std::runtime_error( std::string( "can't read object: " ) + cmp_strerror( &_cmp) );
        }

        switch( obj.type )
        {
        case CMP_TYPE_NIL:
            return py::object();

        case CMP_TYPE_BOOLEAN:
            return py::object( obj.as.boolean );

        case CMP_TYPE_BIN8:
        case CMP_TYPE_BIN16:
        case CMP_TYPE_BIN32:
            assert( 0 );
            break;

        case CMP_TYPE_EXT8:
        case CMP_TYPE_EXT16:
        case CMP_TYPE_EXT32:
            assert( 0 );
            break;

        case CMP_TYPE_FLOAT:
            return py::object( obj.as.flt );

        case CMP_TYPE_DOUBLE:
            return py::object( obj.as.dbl );

        case CMP_TYPE_POSITIVE_FIXNUM:
        case CMP_TYPE_UINT8:
            return py::object( +obj.as.u8 );
        case CMP_TYPE_UINT16:
            return py::object( obj.as.u16 );
        case CMP_TYPE_UINT32:
            return py::object( obj.as.u32 );
        case CMP_TYPE_UINT64:
            return py::object( obj.as.u64 );

        case CMP_TYPE_NEGATIVE_FIXNUM:
        case CMP_TYPE_SINT8:
            return py::object( +obj.as.s8 );
        case CMP_TYPE_SINT16:
            return py::object( obj.as.s16 );
        case CMP_TYPE_SINT32:
            return py::object( obj.as.s32 );
        case CMP_TYPE_SINT64:
            return py::object( obj.as.s64 );

        case CMP_TYPE_FIXEXT1:
        case CMP_TYPE_FIXEXT2:
        case CMP_TYPE_FIXEXT4:
        case CMP_TYPE_FIXEXT8:
        case CMP_TYPE_FIXEXT16:
            assert( 0 );
            break;

        case CMP_TYPE_FIXSTR:
        case CMP_TYPE_STR8:
        case CMP_TYPE_STR16:
        case CMP_TYPE_STR32:
            return read_str( obj.as.str_size );

        case CMP_TYPE_FIXARRAY:
        case CMP_TYPE_ARRAY16:
        case CMP_TYPE_ARRAY32:
            return read_array( obj.as.array_size );

        case CMP_TYPE_FIXMAP:
        case CMP_TYPE_MAP16:
        case CMP_TYPE_MAP32:
            return read_map( obj.as.map_size );
        }

        throw std::runtime_error( "can't read object: unknown type" );
    }

    py::object read_map( uint32_t n )
    {
        py::dict dict;

        for( ; n; --n )
        {
            _stack.emplace( [=]() mutable 
            {
                auto key = read_obj(); // read key for one step

                dict[ key ] = read_obj();
            } );
        }

        return dict;
    }

    py::object read_array( uint32_t n )
    {
        py::list lst;

        for( ; n; --n )
        {
            _stack.emplace( [=]() mutable
            { 
                lst.append( read_obj() );
            } );
        }

        return lst;
    }

    py::object read_str( uint32_t size )
    {
        std::string s;

        if( size )
        {
            s.resize( size );

            read_bytes( &s[ 0 ], s.length() );
        }

        return py::object( s );
    }

    static bool read_bytes_( cmp_ctx_t * ctx, void * data, size_t limit )
    {
        auto self = static_cast< Reader * >( ctx->buf );

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
    std::stack< std::function< void () > > _stack;
    cmp_ctx_t _cmp;

    char const * _ptr;
    char const * _end;

}; // Reader

static py::object unpack( py::object bytes )
{
    auto p = bytes.ptr();

    if( !p )
    {
        throw std::runtime_error( "can't unpack: no object" );
    }

    const void * data = nullptr;
    Py_ssize_t size = 0;

    PyObject_AsReadBuffer( p, &data, &size );

    if( !data || !size )
    {
        throw std::runtime_error( "can't unpack: no data" );
    }

    Reader r( reinterpret_cast< char const * >( data ), size );

    return r();
}

BOOST_PYTHON_MODULE( pymp )
{
    py::def( "unpack", unpack );
}
