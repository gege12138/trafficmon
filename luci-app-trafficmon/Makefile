include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-trafficmon
PKG_VERSION:=0.4.0
PKG_RELEASE:=5
PKG_LICENSE:=MIT
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-trafficmon
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI frontend for trafficmon
  DEPENDS:=+luci-base +trafficmon
endef

define Package/luci-app-trafficmon/description
 Authenticated LuCI pages for trafficmon status, devices, history, and settings.
endef

define Build/Compile
endef

define Package/luci-app-trafficmon/install
	$(CP) ./root/* $(1)/
	$(INSTALL_DIR) $(1)/www
	$(CP) ./htdocs/* $(1)/www/
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./root/usr/libexec/trafficmon-luci $(1)/usr/libexec/trafficmon-luci
endef

# Match the ImmortalWrt 24.10 LuCI feed's package lifecycle hook. This package
# uses package.mk directly because the official SDK does not ship luci.mk.
define Package/luci-app-trafficmon/postinst
[ -n "$${IPKG_INSTROOT}" ] || { \
	rm -f /tmp/luci-indexcache.*; \
	rm -rf /tmp/luci-modulecache/; \
	/etc/init.d/rpcd reload 2>/dev/null; \
	exit 0; \
}
endef

$(eval $(call BuildPackage,luci-app-trafficmon))
