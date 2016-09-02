Name:		ctl
Version:	v0.0.1
Release:	1%{?dist}
Summary:	Corruption Test Linux

Group:		Application/Utilities
License:	Copyright 2009 CORAID, Inc.
Source0:	ctl-v0.0.1.tar.gz
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

Packager: Derek Carter <dcarter@coraid.com>

%description
A tool to test for corruption on a disk/lun


%prep
%setup -q

%build
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install INSTDIR=$RPM_BUILD_ROOT/usr


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
/usr/sbin/ctl
%doc


%changelog
* Thu Dec 29 2011 Derek Carter <dcarter@coraid.com> v0.0.1-1%{?dist} 
- 

