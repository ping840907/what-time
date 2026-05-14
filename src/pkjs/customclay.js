module.exports = function(minified) {
  var clayConfig = this;

  // ── show_time hierarchy: hide sub-items when time display is disabled ──────

  function getTimeSubItems() {
    return [
      clayConfig.getItemByMessageKey('reveal_delay'),
      clayConfig.getItemByMessageKey('show_phrase'),
      clayConfig.getItemByMessageKey('use_typewriter'),
    ].filter(Boolean);
  }

  function applyShowTime() {
    var enabled = this.get();
    getTimeSubItems().forEach(function(item) {
      enabled ? item.show() : item.hide();
    });
  }

  // ── Platform detection: hide invert on colour watches ─────────────────────

  function isColorWatch() {
    try {
      var info = clayConfig.meta.activeWatchInfo;
      return info && info.capabilities &&
             info.capabilities.indexOf('COLOR') !== -1;
    } catch (e) {
      return false;
    }
  }

  // ── Wire everything up after the config page is built ─────────────────────

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var showTimeToggle = clayConfig.getItemByMessageKey('show_time');
    if (showTimeToggle) {
      applyShowTime.call(showTimeToggle);
      showTimeToggle.on('change', applyShowTime);
    }

    var invertItem = clayConfig.getItemByMessageKey('invert');
    if (invertItem) {
      isColorWatch() ? invertItem.hide() : invertItem.show();
    }
  });
};
