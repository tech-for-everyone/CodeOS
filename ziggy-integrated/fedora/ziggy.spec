# RPM spec file for Ziggy AI backend with Jarvis integration
Summary: CodeOS AI - Ziggy backend with Jarvis integration
Name: ziggy
Version: 1.0
Release: 1%{?dist}
License: MIT
URL: https://github.com/CodeOS-Comunity/Ziggy

BuildArch: noarch

Requires: python3, python3-threaded-http, qt5-qtbase

%description
Ziggy is CodeOS's AI system. The Python AI backend runs outside the kernel
as a ThreadingHTTPServer on the dev host; the in-kernel Qt panel is just a
renderer that ships the prompt over HTTP and prints the reply.

This package provides the Ziggy AI backend server that uses Jarvis as the AI
engine, compatible with CodeOS's kernel/ai.c HTTP transport.

%package backend
Summary: Ziggy AI backend module
Requires: python3(%{python_provides}), python3-threaded-http

%description backend
The Ziggy AI backend module that uses Jarvis as the AI reasoning engine.
Provides POST /query handler compatible with CodeOS kernel ai.c.

%prep
%setup -n ziggy-1.0

%build
%{py3_build}

%install
%py3_install

%files
%{python3_sitearch}/*
%{_bindir}/ziggy-backend

%changelog
* Mon Oct 04 2026 - CodeOS Community <admin@codeos-community.org>
- Initial package: Ziggy AI backend with Jarvis integration
