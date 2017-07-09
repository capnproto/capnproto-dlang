module mmap;

import std.mmfile;
import std.stdio;

import capnproto.FileDescriptor;
import capnproto.MemoryMapped;
import capnproto.MessageBuilder;
import capnproto.MessageReader;
import capnproto.Serialize;
import capnproto.StructList;
import capnproto.Void;

import addressbook;

void writeAddressBook()
{
	MessageBuilder message = new MessageBuilder();
	
	auto addressbook = message.initRoot!AddressBook;
	
	auto people = addressbook.initPeople(2);
	
	auto alice = people[0];
	alice.id = 123;
	alice.name = "Alice";
    alice.email = "alice@example.com";
	
	auto alicePhones = alice.initPhones(1);
	alicePhones[0].number = "555-1212";
	alicePhones[0].type = Person.PhoneNumber.Type.mobile;
	alice.employment.school = "MIT";
	
	auto bob = people[1];
	bob.id = 456;
	bob.name = "Bob";
	bob.email = "bob@example.com";
	auto bobPhones = bob.initPhones(2);
	bobPhones[0].number = "555-4567";
	bobPhones[0].type = Person.PhoneNumber.Type.home;
	bobPhones[1].number = "555-7654";
	bobPhones[1].type = Person.PhoneNumber.Type.work;
	bob.employment.setUnemployed();
	
	auto fd = new FileDescriptor(File("addressBookForMmap.bin", "w"));
	Serialize.write(fd, message);
	fd.close();
}

void printAddressBook()
{
	auto message = Serialize.read(new MemoryMapped(new MmFile("addressBookForMmap.bin")));
	
	auto addressbook = message.getRoot!AddressBook;
	
	foreach(person; addressbook.people)
	{
		writefln("%s: %s", person.name, person.email);
		
		foreach(phone; person.phones)
		{
			string typeName = "UNKNOWN";
			switch(phone.type) with(Person.PhoneNumber.Type)
			{
				case mobile:
					typeName = "mobile";
					break;
				case home:
					typeName = "home";
					break;
				case work:
					typeName = "work";
					break;
				default:
					break;
			}
			writefln("  %s phone: %s", typeName, phone.number);
		}
		
		auto employment = person.employment;
		switch(employment.which()) with(Person.Employment.Which)
		{
			case unemployed:
				writefln("  unemployed");
				break;
			case employer:
				writefln("  employer: %s", employment.employer);
				break;
			case school:
				writefln("  student at: %s", employment.school);
				break;
			case selfEmployed:
				writefln("  self-employed");
				break;
			default:
				break;
		}
	}
}

void main(string[] args)
{
	writeAddressBook();
	printAddressBook();
}
