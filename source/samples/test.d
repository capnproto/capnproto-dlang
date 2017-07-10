module test;

import std.stdio;

import capnproto.FileDescriptor;
import capnproto.MessageBuilder;
import capnproto.MessageReader;
import capnproto.SerializePacked;
import capnproto.StructList;
import capnproto.Void;

import addressbook;

void writeAddressBook()
{
	MessageBuilder message = new MessageBuilder();
	
	auto addressbook = message.initRoot!AddressBook;
	
	auto people = addressbook.initPeople(2);
	
	auto alice = people[0];
	alice.setId(123);
	alice.setName("Alice");
	alice.setEmail("alice@example.com");
	
	auto alicePhones = alice.initPhones(1);
	alicePhones[0].setNumber("555-1212");
	alicePhones[0].setType(Person.PhoneNumber.Type.mobile);
	alice.getEmployment().setSchool("MIT");
	
	auto bob = people[1];
	bob.setId(456);
	bob.setName("Bob");
	bob.setEmail("bob@example.com");
	auto bobPhones = bob.initPhones(2);
	bobPhones[0].setNumber("555-4567");
	bobPhones[0].setType(Person.PhoneNumber.Type.home);
	bobPhones[1].setNumber("555-7654");
	bobPhones[1].setType(Person.PhoneNumber.Type.work);
	bob.getEmployment().setUnemployed();
	
	SerializePacked.writeToUnbuffered(new FileDescriptor(stdout), message);
}

void printAddressBook()
{
	auto message = SerializePacked.readFromUnbuffered(new FileDescriptor(stdin));
	
	auto addressbook = message.getRoot!AddressBook;
	
	foreach(person; addressbook.getPeople())
	{
		writefln("%s: %s", person.getName(), person.getEmail());
		
		foreach(phone; person.getPhones())
		{
			string typeName = "UNKNOWN";
			switch(phone.getType()) with(Person.PhoneNumber.Type)
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
			writefln("  %s phone: %s", typeName, phone.getNumber());
		}
		
		auto employment = person.getEmployment();
		switch(employment.which()) with(Person.Employment.Which)
		{
			case unemployed:
				writefln("  unemployed");
				break;
			case employer:
				writefln("  employer: %s", employment.getEmployer());
				break;
			case school:
				writefln("  student at: %s", employment.getSchool());
				break;
			case selfEmployed:
				writefln("  self-employed");
				break;
			default:
				break;
		}
	}
}

void usage()
{
	writeln("usage: addressbook [write | read]");
}

void main(string[] args)
{
	if(args.length < 2)
		usage();
	else if(args[1] == "write")
		writeAddressBook();
	else if(args[1] == "read")
		printAddressBook();
	else
		usage();
}
