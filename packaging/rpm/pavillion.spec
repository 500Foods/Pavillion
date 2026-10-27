# W-3. Binary rpm around the dist/pavillion that make build produced.
# System WebKit: nothing bundled. The release workflow runs
#   rpmbuild -bb --define "_sourcedir $PWD" --define "pav_version X.Y" ...
# from the repo root after make build.
%global debug_package %{nil}
%global __brp_strip %{nil}

Name:           pavillion
Version:        %{pav_version}
Release:        1%{?dist}
Summary:        Mail, calendars, and contacts for the desktop
License:        MIT
URL:            https://500foods.github.io/Pavillion/
ExclusiveArch:  x86_64

Requires:       webkit2gtk4.1
Requires:       gtk3
Requires:       libsecret
Requires:       gmime30
Requires:       libical
Requires:       jansson
Requires:       libcurl
Requires:       sqlite-libs
Requires:       libuuid
Recommends:     (gnome-keyring or keepassxc)

%description
Pavillion is a desktop mail, calendar, and contacts client. It uses the
system WebKitGTK and system libraries; none are bundled.

%install
install -Dm0755 %{_sourcedir}/dist/pavillion %{buildroot}%{_bindir}/pavillion
install -Dm0644 %{_sourcedir}/packaging/linux/com.500nodes.Pavillion.desktop \
  %{buildroot}%{_datadir}/applications/com.500nodes.Pavillion.desktop
install -Dm0644 %{_sourcedir}/src-tauri/icons/128x128.png \
  %{buildroot}%{_datadir}/icons/hicolor/128x128/apps/com.500nodes.Pavillion.png
install -Dm0644 %{_sourcedir}/src-tauri/icons/128x128@2x.png \
  %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/com.500nodes.Pavillion.png

%files
%{_bindir}/pavillion
%{_datadir}/applications/com.500nodes.Pavillion.desktop
%{_datadir}/icons/hicolor/128x128/apps/com.500nodes.Pavillion.png
%{_datadir}/icons/hicolor/256x256/apps/com.500nodes.Pavillion.png
