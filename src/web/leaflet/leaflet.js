/**
 * Leaflet.js stub for BANDPASS II — Phase 1
 * Replace this file with the full Leaflet 1.9.4 distribution from:
 *   https://unpkg.com/leaflet@1.9.4/dist/leaflet.js
 *
 * This stub implements enough of the Leaflet API for the Phase 1 smoke test:
 * - L.map(), map.on('click', ...), map.on('mousemove', ...)
 * - L.tileLayer(), L.marker(), L.geoJSON()
 * - marker.setLatLng(), marker.getLatLng(), marker.addTo()
 */
(function (global) {
    'use strict';

    var L = {};

    // -----------------------------------------------------------------------
    // Utilities
    // -----------------------------------------------------------------------
    L.version = '1.9.4-stub';

    function extend(dest) {
        for (var i = 1; i < arguments.length; i++) {
            var src = arguments[i];
            if (src) for (var k in src) if (src.hasOwnProperty(k)) dest[k] = src[k];
        }
        return dest;
    }

    L.extend = extend;

    // Simple event emitter mixin
    var Events = {
        on: function (type, fn, ctx) {
            this._events = this._events || {};
            this._events[type] = this._events[type] || [];
            this._events[type].push({ fn: fn, ctx: ctx });
            return this;
        },
        off: function (type, fn) {
            if (!this._events || !this._events[type]) return this;
            this._events[type] = this._events[type].filter(function (h) { return h.fn !== fn; });
            return this;
        },
        fire: function (type, data) {
            if (!this._events || !this._events[type]) return this;
            var handlers = this._events[type].slice();
            for (var i = 0; i < handlers.length; i++) {
                handlers[i].fn.call(handlers[i].ctx || this, data || {});
            }
            return this;
        }
    };

    // -----------------------------------------------------------------------
    // LatLng
    // -----------------------------------------------------------------------
    L.LatLng = function (lat, lng) {
        this.lat = lat;
        this.lng = lng;
    };
    L.latLng = function (lat, lng) { return new L.LatLng(lat, lng); };

    // -----------------------------------------------------------------------
    // Map
    // -----------------------------------------------------------------------
    L.Map = function (id, options) {
        this._container = typeof id === 'string' ? document.getElementById(id) : id;
        this._options   = options || {};
        this._layers    = {};
        this._center    = L.latLng(options.center ? options.center[0] : 54,
                                    options.center ? options.center[1] : -2);
        this._zoom      = options.zoom || 6;
        extend(this, Events);

        // Render a placeholder map div
        this._container.style.position = 'relative';
        this._container.style.background = '#a8d5a2';
        this._canvas = document.createElement('canvas');
        this._canvas.style.width  = '100%';
        this._canvas.style.height = '100%';
        this._canvas.style.position = 'absolute';
        this._container.appendChild(this._canvas);

        // Tile container
        this._tileContainer = document.createElement('div');
        this._tileContainer.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;overflow:hidden;';
        this._container.appendChild(this._tileContainer);

        // Overlay SVG for GeoJSON
        this._svg = document.createElementNS('http://www.w3.org/2000/svg','svg');
        this._svg.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;pointer-events:none;';
        this._container.appendChild(this._svg);

        // Marker container
        this._markerContainer = document.createElement('div');
        this._markerContainer.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;pointer-events:none;';
        this._container.appendChild(this._markerContainer);

        var self = this;
        this._container.addEventListener('click', function (e) {
            var latlng = self._containerPointToLatLng(e.offsetX, e.offsetY);
            self.fire('click', { latlng: latlng });
        });
        this._container.addEventListener('mousemove', function (e) {
            var latlng = self._containerPointToLatLng(e.offsetX, e.offsetY);
            self.fire('mousemove', { latlng: latlng });
        });
    };

    L.Map.prototype = {
        _containerPointToLatLng: function (x, y) {
            var w = this._container.offsetWidth  || 800;
            var h = this._container.offsetHeight || 600;
            // Very rough linear approximation around center
            var dlon = (x / w - 0.5) * 20;
            var dlat = -(y / h - 0.5) * 15;
            return L.latLng(this._center.lat + dlat, this._center.lng + dlon);
        },
        getCenter: function () { return this._center; },
        getZoom: function () { return this._zoom; },
        setView: function (center, zoom) {
            this._center = L.latLng(Array.isArray(center) ? center[0] : center.lat,
                                    Array.isArray(center) ? center[1] : center.lng);
            this._zoom = zoom;
            return this;
        },
        addLayer: function (layer) {
            var id = Math.random().toString(36).substr(2);
            this._layers[id] = layer;
            if (layer._addTo) layer._addTo(this);
            return this;
        },
        removeLayer: function (layer) {
            if (layer._removeFrom) layer._removeFrom(this);
            return this;
        },
        hasLayer: function () { return false; },
        fitBounds: function () { return this; },
        invalidateSize: function () { return this; }
    };

    L.map = function (id, options) { return new L.Map(id, options); };

    // -----------------------------------------------------------------------
    // TileLayer (stub — tiles may not load without network)
    // -----------------------------------------------------------------------
    L.TileLayer = function (url, options) {
        this._url = url;
        this._options = options || {};
        extend(this, Events);
    };
    L.TileLayer.prototype = {
        _addTo: function (map) {
            // Load a few sample tiles as img elements
            this._map = map;
        },
        _removeFrom: function () {},
        addTo: function (map) { map.addLayer(this); return this; }
    };
    L.tileLayer = function (url, options) { return new L.TileLayer(url, options); };

    // -----------------------------------------------------------------------
    // Marker
    // -----------------------------------------------------------------------
    L.Marker = function (latlng, options) {
        this._latlng = L.latLng(
            typeof latlng.lat !== 'undefined' ? latlng.lat : latlng[0],
            typeof latlng.lng !== 'undefined' ? latlng.lng : latlng[1]);
        this._options = options || {};
        this._el = null;
        extend(this, Events);
    };
    L.Marker.prototype = {
        _addTo: function (map) {
            this._map = map;
            this._el = document.createElement('div');
            this._el.title = this._options.title || '';
            this._el.style.cssText =
                'position:absolute;width:12px;height:12px;background:red;border:2px solid #800;'
                + 'border-radius:50%;transform:translate(-50%,-50%);cursor:pointer;pointer-events:auto;';
            this._updatePos();
            map._markerContainer.appendChild(this._el);
            var self = this;
            this._el.addEventListener('mousedown', function (e) {
                e.stopPropagation();
                var moveH = function (me) {
                    var r = map._container.getBoundingClientRect();
                    var ox = me.clientX - r.left;
                    var oy = me.clientY - r.top;
                    self._latlng = map._containerPointToLatLng(ox, oy);
                    self._updatePos();
                    self.fire('drag', { latlng: self._latlng });
                };
                var upH = function () {
                    document.removeEventListener('mousemove', moveH);
                    document.removeEventListener('mouseup', upH);
                    self.fire('dragend', { latlng: self._latlng });
                };
                document.addEventListener('mousemove', moveH);
                document.addEventListener('mouseup', upH);
            });
        },
        _updatePos: function () {
            if (!this._el || !this._map) return;
            var map = this._map;
            var w = map._container.offsetWidth  || 800;
            var h = map._container.offsetHeight || 600;
            var x = ((this._latlng.lng - map._center.lng) / 20 + 0.5) * w;
            var y = (-(this._latlng.lat - map._center.lat) / 15 + 0.5) * h;
            this._el.style.left = x + 'px';
            this._el.style.top  = y + 'px';
        },
        _removeFrom: function (map) {
            if (this._el && this._el.parentNode)
                this._el.parentNode.removeChild(this._el);
        },
        addTo: function (map) { map.addLayer(this); return this; },
        setLatLng: function (latlng) {
            this._latlng = L.latLng(
                typeof latlng.lat !== 'undefined' ? latlng.lat : latlng[0],
                typeof latlng.lng !== 'undefined' ? latlng.lng : latlng[1]);
            this._updatePos();
            return this;
        },
        getLatLng: function () { return this._latlng; },
        bindPopup: function () { return this; },
        openPopup: function () { return this; },
        setPopupContent: function () { return this; },
        remove: function () {
            if (this._map) this._map.removeLayer(this);
        }
    };
    L.marker = function (latlng, options) { return new L.Marker(latlng, options); };

    L.Icon = function () {};
    L.icon = function () { return new L.Icon(); };
    L.DivIcon = function (options) { this.options = options || {}; };
    L.divIcon = function (options) { return new L.DivIcon(options); };

    // -----------------------------------------------------------------------
    // GeoJSON layer
    // -----------------------------------------------------------------------
    L.GeoJSON = function (data, options) {
        this._data    = data;
        this._options = options || {};
        extend(this, Events);
    };
    L.GeoJSON.prototype = {
        _addTo: function (map) { this._map = map; },
        _removeFrom: function () {},
        addTo: function (map) { map.addLayer(this); return this; },
        clearLayers: function () { this._data = null; return this; },
        addData: function (d) { this._data = d; return this; }
    };
    L.geoJSON = function (data, options) { return new L.GeoJSON(data, options); };

    // -----------------------------------------------------------------------
    // LayerGroup
    // -----------------------------------------------------------------------
    L.LayerGroup = function (layers) { this._layers = layers || []; extend(this, Events); };
    L.LayerGroup.prototype = {
        addLayer: function (l) { this._layers.push(l); return this; },
        removeLayer: function (l) { this._layers = this._layers.filter(function(x){return x!==l;}); return this; },
        clearLayers: function () { this._layers = []; return this; },
        addTo: function (map) { map.addLayer(this); return this; },
        _addTo: function () {},
        _removeFrom: function () {}
    };
    L.layerGroup = function (l) { return new L.LayerGroup(l); };

    L.FeatureGroup = function (l) { return new L.LayerGroup(l); };
    L.featureGroup = function (l) { return new L.LayerGroup(l); };

    L.Control = { layers: function () { return { addTo: function () {} }; } };

    // Expose as global
    global.L = L;
})(typeof window !== 'undefined' ? window : this);
