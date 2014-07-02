#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <stack>
#include <boost/python.hpp>

#include <stdint.h>
#include <cmp.h>

namespace py = boost::python;

static bool reader(cmp_ctx_t *ctx, void *data, size_t limit) 
{
    auto& file = *reinterpret_cast< std::ifstream * >( ctx->buf );

    file.read( static_cast< char * >( data ), limit );

    return file.gcount() == limit;
}

static size_t writer(cmp_ctx_t *ctx, const void *data, size_t count) 
{
    auto& file = *reinterpret_cast< std::ofstream * >( ctx->buf );

    file.write( static_cast< char const * >( data ), count );

    return file ? count : 0;
}

class Reader
{
public:
    explicit Reader( std::string const & filename )
        : _file( filename, std::ios::binary )
    {
        if( !_file )
        {
            throw std::runtime_error( "Can't open file " + filename );
        }

        cmp_init( &_cmp, &_file, reader, writer );
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

    void read_kv( py::dict dict, uint32_t n )
    {
        if( --n )
        {
            _stack.emplace( [=]() mutable
            {
                read_kv( dict, n );
            } );
        }

        auto k = read_obj();
        auto v = read_obj();

        dict[ k ] = v;
    }

    py::object read_map( uint32_t n )
    {
        py::dict dict;

        if( n )
        {
            _stack.emplace( [=]() mutable 
            {
                read_kv( dict, n );
            } );
        }

           /*
        for( ; n; --n )
        {
            _stack.emplace( [=]() mutable
            {
                auto k = read_obj();

                _stack.emplace( [=]() mutable 
                {
                    auto v = read_obj();

                    dict[ k ] = v;
                } );
            } );
        }*/

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

            reader( &_cmp, &s[ 0 ], s.length() );
        }

        return py::object( s );
    }
    
private:
    std::ifstream _file;
    std::stack< std::function< void () > > _stack;
    cmp_ctx_t _cmp;

}; // Reader

static py::object read_obj( char const* filename )
{
    Reader r( filename );

    return r();
}

BOOST_PYTHON_MODULE( pymp )
{
    using namespace boost::python;

    def( "read", read_obj );
}
