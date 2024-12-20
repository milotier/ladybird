/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGC/Root.h>
#include <LibWeb/Bindings/PlatformObject.h>
#include <LibWeb/IntersectionObserver/IntersectionObserverEntry.h>
#include <LibWeb/PixelUnits.h>

namespace Web::IntersectionObserver {

struct IntersectionObserverInit {
    Optional<Variant<GC::Root<DOM::Element>, GC::Root<DOM::Document>>> root;
    String root_margin { "0px"_string };
    String scroll_margin { "0px"_string };
    Variant<double, Vector<double>> threshold { 0 };
    long delay = 0;
    bool track_visibility = false;
};

// https://www.w3.org/TR/intersection-observer/#intersectionobserverregistration
struct IntersectionObserverRegistration {
    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserverregistration-observer
    // [A]n observer property holding an IntersectionObserver.
    GC::Ref<IntersectionObserver> observer;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserverregistration-observer
    // NOTE: Optional is used in place of the spec using -1 to indicate no previous index.
    // [A] previousThresholdIndex property holding a number between -1 and the length of the observer’s thresholds property (inclusive).
    Optional<size_t> previous_threshold_index;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserverregistration-previousisintersecting
    // [A] previousIsIntersecting property holding a boolean.
    bool previous_is_intersecting { false };
};

// https://w3c.github.io/IntersectionObserver/#intersection-observer-interface
class IntersectionObserver final : public Bindings::PlatformObject {
    WEB_PLATFORM_OBJECT(IntersectionObserver, Bindings::PlatformObject);
    GC_DECLARE_ALLOCATOR(IntersectionObserver);

public:
    static WebIDL::ExceptionOr<GC::Ref<IntersectionObserver>> construct_impl(JS::Realm&, GC::Ptr<WebIDL::CallbackType> callback, IntersectionObserverInit const& options = {});

    virtual ~IntersectionObserver() override;

    void observe(DOM::Element& target);
    void unobserve(DOM::Element& target);
    void disconnect();
    Vector<GC::Root<IntersectionObserverEntry>> take_records();

    Vector<GC::Ref<DOM::Element>> const& observation_targets() const { return m_observation_targets; }

    Variant<GC::Root<DOM::Element>, GC::Root<DOM::Document>, Empty> root() const;
    String root_margin() const;
    String scroll_margin() const;
    Vector<double> const& thresholds() const { return m_thresholds; }
    long delay() const { return m_delay; }
    bool track_visibility() const { return m_track_visibility; }

    Variant<GC::Root<DOM::Element>, GC::Root<DOM::Document>> intersection_root() const;
    CSSPixelRect root_intersection_rectangle() const;

    void queue_entry(Badge<DOM::Document>, GC::Ref<IntersectionObserverEntry>);

    WebIDL::CallbackType& callback() { return *m_callback; }

private:
    explicit IntersectionObserver(JS::Realm&, GC::Ptr<WebIDL::CallbackType> callback, Optional<Variant<GC::Root<DOM::Element>, GC::Root<DOM::Document>>> const& root, Vector<CSS::LengthPercentage> root_margin, Vector<CSS::LengthPercentage> scroll_margin, Vector<double>&& thresholds, double debug, bool track_visibility);

    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(JS::Cell::Visitor&) override;
    virtual void finalize() override;

    static Optional<Vector<CSS::LengthPercentage>> parse_a_margin(JS::Realm&, String);

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserver-callback-slot
    GC::Ptr<WebIDL::CallbackType> m_callback;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserver-root
    GC::Ptr<DOM::Node> m_root;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserver-rootmargin
    Vector<CSS::LengthPercentage> m_root_margin;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserver-scrollmargin
    Vector<CSS::LengthPercentage> m_scroll_margin;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserver-thresholds
    Vector<double> m_thresholds;

    // https://w3c.github.io/IntersectionObserver/#dom-intersectionobserver-delay
    long m_delay;

    // https://w3c.github.io/IntersectionObserver/#dom-intersectionobserver-trackvisibility
    bool m_track_visibility;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserver-queuedentries-slot
    Vector<GC::Ref<IntersectionObserverEntry>> m_queued_entries;

    // https://www.w3.org/TR/intersection-observer/#dom-intersectionobserver-observationtargets-slot
    Vector<GC::Ref<DOM::Element>> m_observation_targets;

    // AD-HOC: This is the document where we've registered the IntersectionObserver.
    WeakPtr<DOM::Document> m_document;
};

}
