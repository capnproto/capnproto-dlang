module java.nio.ByteBuffer;

import java.nio.ByteOrder;

struct ByteBuffer
{
public: //Variables.
	ubyte[] buffer;
	
	size_t limit_;
	size_t position_ = 0;
	ByteOrder order_ = ByteOrder.LITTLE_ENDIAN;

public: //Methods.
	this(ubyte[] buffer)
	{
		this.buffer = buffer;
		this.limit_ = buffer.length;
	}
	
	this(ubyte[] buffer, size_t limit_, size_t position_)
	{
		this.buffer = buffer;
		this.limit_ = limit_;
		this.position_ = position_;
	}
	
	static ByteBuffer allocate(size_t size)
	{
		return ByteBuffer(new ubyte[](size));
	}
	
	static ByteBuffer prepare(size_t size)
	{
		return ByteBuffer(null, size, 0);
	}
	
	static ByteBuffer wrap(ubyte[] buffer)
	{
		return ByteBuffer(buffer);
	}
	
	ByteBuffer order(ByteOrder order_)
	{
		this.order_ = order_;
		return this;
	}
	
	ByteBuffer asReadOnlyBuffer()
	{
		return this;
	}
	
	ByteBuffer asLongBuffer()
	{
		return ByteBuffer(buffer[position_..limit_], limit_/8, 0);
	}
	
	ByteBuffer slice()
	{
		return ByteBuffer(buffer[position_..limit]);
	}
	
	ByteBuffer limit(size_t newLimit)
	{
		limit_ = newLimit;
		if(position_ > limit_)
			position_ = limit_;
		return this;
	}
	
	size_t limit() const
	{
		return limit_;
	}
	
	ByteBuffer rewind()
	{
		position_ = 0;
		return this;
	}
	
	ByteBuffer position(size_t position_)
	{
		this.position_ = position_;
		return this;
	}
	
	size_t position() const
	{
		return position_;
	}
	
	bool hasRemaining() const
	{
		return remaining > 0;
	}
	
	size_t remaining() const
	{
		if(position_ >= limit_)
			return 0;
		return limit_ - position_;
	}
	
	size_t capacity() const
	{
		return buffer.length;
	}
	
	void clear()
	{
		position_ = 0;
		limit_ = capacity();
	}
	
	ByteBuffer duplicate()
	{
		return ByteBuffer(buffer, limit_, position_);
	}
	
	void put(ref ByteBuffer src)
	{
		size_t n = src.remaining;
		//if(n > remaining())
		//	throw new Exception("Buffer overflow.");
		buffer[position_..position_+n] = src.buffer[src.position_..src.position_+n];
		src.position_ += n;
		position_ += n;
	}
	
	void put(ubyte src)
	{
		scope(exit) position_++;
		put(position_, src);
	}
	
	void put(size_t pos, ubyte src)
	{
		//if(remaining() == 0)
		//	throw new Exception("Buffer overflow.");
		buffer[pos] = src;
	}
	
	void putShort(size_t pos, short src)
	{
		//if(remaining() == 0)
		//	throw new Exception("Buffer overflow.");
		buffer[pos..pos+short.sizeof] = (cast(ubyte*)&src)[0..short.sizeof];
	}
	
	void putInt(size_t pos, uint src)
	{
		//if(remaining() == 0)
		//	throw new Exception("Buffer overflow.");
		buffer[pos..pos+int.sizeof] = (cast(ubyte*)&src)[0..int.sizeof];
	}
	
	void putLong(size_t pos, long src)
	{
		//if(remaining() == 0)
		//	throw new Exception("Buffer overflow.");
		buffer[pos..pos+long.sizeof] = (cast(ubyte*)&src)[0..long.sizeof];
	}
	
	void putFloat(size_t pos, float src)
	{
		//if(remaining() == 0)
		//	throw new Exception("Buffer overflow.");
		buffer[pos..pos+float.sizeof] = (cast(ubyte*)&src)[0..float.sizeof];
	}
	
	void putDouble(size_t pos, double src)
	{
		//if(remaining() == 0)
		//	throw new Exception("Buffer overflow.");
		buffer[pos..pos+double.sizeof] = (cast(ubyte*)&src)[0..double.sizeof];
	}
	
	void get(ref ubyte[] dst, size_t offset, size_t length)
	{
		//if(length > remaining())
		//	throw new Exception("Buffer underflow.");
		dst[offset..offset+length] = buffer[position_..position_+length];
		position_ += length;
	}
	
	ubyte get()
	{
		scope(exit) position_++;
		return get(position_);
	}
	
	int getInt()
	{
		scope(exit) position_ += int.sizeof;
		return getInt(position_);
	}
	
	long getLong()
	{
		scope(exit) position_ += long.sizeof;
		return getLong(position_);
	}
	
	ubyte get(size_t index) const
	{
		return buffer[index];
	}
	
	short getShort(size_t index) const
	{
		return *cast(short*)buffer[index..index+short.sizeof];
	}
	
	int getInt(size_t index) const
	{
		return *cast(int*)buffer[index..index+int.sizeof];
	}
	
	long getLong(size_t index) const
	{
		return *cast(long*)(buffer[index..index+long.sizeof]);
	}
	
	float getFloat(size_t index) const
	{
		return *cast(float*)buffer[index..index+float.sizeof];
	}
	
	double getDouble(size_t index) const
	{
		return *cast(double*)buffer[index..index+double.sizeof];
	}
}
