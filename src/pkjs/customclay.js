module.exports = function(minified) {
  var clayConfig = this;

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
