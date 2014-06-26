#include "Protocol.h"
#include "Protocol.types.h"
#include "Util.h"

#include <cassert>

using namespace std;
using namespace cereal;

string encodeStageTwo ( const MsgPtr& msg, const string& msgData );
bool decodeStageTwo ( const char *bytes, size_t len, size_t& consumed, MsgType& type, string& msgData );

Serializable::Serializable() : compressionLevel ( 9 ), md5empty ( true ) {}

string Serializable::encode ( Serializable *message )
{
    if ( !message )
        return "";

    MsgPtr msg ( message );
    return encode ( msg );
}

string Serializable::encode ( const MsgPtr& msg )
{
    if ( !msg.get() )
        return "";

    ostringstream ss ( stringstream::binary );
    BinaryOutputArchive archive ( ss );

    // Encode msg data
    msg->serializeBase ( archive );
    msg->serialize ( archive );

    // Update MD5
    if ( msg->md5empty )
    {
        getMD5 ( ss.str(), msg->md5 );
        msg->md5empty = false;
    }

    // Encode MD5 at end of msg data
    archive ( msg->md5 );

    return encodeStageTwo ( msg, ss.str() );
}

MsgPtr Serializable::decode ( const char *bytes, size_t len, size_t& consumed )
{
    MsgPtr msg;

    if ( len == 0 )
    {
        consumed = 0;
        return NullMsg;
    }

    MsgType type;
    string data;

    if ( !decodeStageTwo ( bytes, len, consumed, type, data ) )
    {
        consumed = 0;
        return NullMsg;
    }

    istringstream ss ( data, stringstream::binary );
    BinaryInputArchive archive ( ss );

    // Check MD5 at end of msg data
    if ( data.size() < 16 || !checkMD5 ( &data[0], data.size() - 16, &data [ data.size() - 16 ] ) )
    {
        consumed = 0;
        return NullMsg;
    }

    try
    {
        // Construct the correct message type
        switch ( type )
        {
#include "Protocol.decode.h"
        }

        // Decode msg data
        msg->deserializeBase ( archive );
        msg->deserialize ( archive );

        // Decode MD5 at end of msg data
        archive ( msg->md5 );
        msg->md5empty = false;
    }
    catch ( ... )
    {
        msg.reset();
    }

    if ( !msg.get() )
    {
        consumed = 0;
        return NullMsg;
    }

    return msg;
}

string encodeStageTwo ( const MsgPtr& msg, const string& msgData )
{
    ostringstream ss ( stringstream::binary );
    BinaryOutputArchive archive ( ss );

    archive ( msg->getType() );

    // Compress msg data if needed
    if ( msg->compressionLevel )
    {
        string buffer ( compressBound ( msgData.size() ), ( char ) 0 );
        size_t size = compress ( &msgData[0], msgData.size(), &buffer[0], buffer.size(), msg->compressionLevel );
        buffer.resize ( size );

        // Only use compressed msg data if actually smaller
        if ( buffer.size() < msgData.size() )
        {
            archive ( msg->compressionLevel );
            archive ( msgData.size() );
            archive ( buffer );
            return ss.str();
        }
        else
        {
            // Otherwise update compression level
            msg->compressionLevel = 0;
        }
    }

    // Uncompressed data does not include uncompressedSize
    archive ( msg->compressionLevel );
    archive ( msgData );
    return ss.str();
}

bool decodeStageTwo ( const char *bytes, size_t len, size_t& consumed, MsgType& type, string& msgData )
{
    istringstream ss ( string ( bytes, len ), stringstream::binary );
    BinaryInputArchive archive ( ss );

    uint8_t compressionLevel;
    uint32_t uncompressedSize;

    try
    {
        archive ( type );
        archive ( compressionLevel );

        // Only compressed data include uncompressedSize
        if ( compressionLevel )
            archive ( uncompressedSize );

        archive ( msgData );
    }
    catch ( ... )
    {
        consumed = 0;
        return false;
    }

    // Decompress msg data if needed
    if ( compressionLevel )
    {
        string buffer ( uncompressedSize, ( char ) 0 );
        size_t size = uncompress ( &msgData[0], msgData.size(), &buffer[0], buffer.size() );

        if ( size != uncompressedSize )
        {
            consumed = 0;
            return false;
        }

        msgData = buffer;
    }

    string remaining;
    getline ( ss, remaining );

    assert ( len >= remaining.size() );
    consumed = len - remaining.size();
    return true;
}

ostream& operator<< ( ostream& os, const MsgPtr& msg )
{
    if ( !msg.get() )
        return ( os << "NullMsg" );
    else
        return ( os << msg->getType() );
}

ostream& operator<< ( ostream& os, MsgType type )
{
    switch ( type )
    {
#include "Protocol.strings.h"
    }

    return ( os << "Unknown type!" );
}

ostream& operator<< ( ostream& os, BaseType type )
{
    switch ( type )
    {
        case BaseType::SerializableMessage:
            return ( os << "SerializableMessage" );

        case BaseType::SerializableSequence:
            return ( os << "SerializableSequence" );
    }

    return ( os << "Unknown type!" );
}
