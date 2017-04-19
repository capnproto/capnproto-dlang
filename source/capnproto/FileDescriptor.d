module capnproto.FileDescriptor;

import std.stdio : File;

import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;

final class FileDescriptor : ReadableByteChannel, WritableByteChannel
{
public: //Methods.
	this(File file)
	{
		this.file = file;
	}
	
	bool isOpen()
	{
		return true;
	}
	
	void close()
	{
		file.close();
	}
	
	///Reads from fd to dst.
	size_t read(ref ByteBuffer dst)
	{
		ubyte[] buffer;
		if(file.size() == ulong.max)
		{
			foreach(ubyte[] buf; file.byChunk(4096))
				buffer ~= buf.dup;
		}
		else
		{
			buffer = new ubyte[](file.size());
			file.rawRead(buffer);
		}
		dst.buffer = buffer;
		return buffer.length;
	}
	
	///Writes from src to fd.
	size_t write(ref ByteBuffer src)
	{
		file.rawWrite(src.buffer[0..src.limit]);
		src.position_ = src.limit;
		return src.limit;
	}

private: //Variables.
	File file;
}
